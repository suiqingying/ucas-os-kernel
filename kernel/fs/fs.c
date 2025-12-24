#include <os/fs.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/time.h>
#include <printk.h>

#define SUPERBLOCK_BLOCK 0
#define INODE_BITMAP_BLOCKS ((FS_INODE_NUM + 8 * BLOCK_SIZE - 1) / (8 * BLOCK_SIZE))
#define BLOCK_BITMAP_BLOCKS ((FS_TOTAL_BLOCKS + 8 * BLOCK_SIZE - 1) / (8 * BLOCK_SIZE))
#define INODE_TABLE_BLOCKS ((FS_INODE_NUM * sizeof(inode_t) + BLOCK_SIZE - 1) / BLOCK_SIZE)

#define INODE_BITMAP_START (SUPERBLOCK_BLOCK + 1)
#define BLOCK_BITMAP_START (INODE_BITMAP_START + INODE_BITMAP_BLOCKS)
#define INODE_TABLE_START (BLOCK_BITMAP_START + BLOCK_BITMAP_BLOCKS)
#define DATA_BLOCK_START (INODE_TABLE_START + INODE_TABLE_BLOCKS)

#define ROOT_INO 0

static superblock_t superblock;
static uint8_t *inode_bitmap;
static uint8_t *block_bitmap;
static inode_t *inode_table;
static fdesc_t fdesc_array[NUM_FDESCS];
static int fs_ready = 0;

typedef struct cache_entry {
    uint32_t block;
    uint8_t *data;
    int dirty;
    struct cache_entry *next;
} cache_entry_t;

#define CACHE_BUCKETS 128
static cache_entry_t *cache_table[CACHE_BUCKETS];
static uint64_t cache_last_flush = 0;

typedef struct dcache_entry {
    uint32_t parent;
    uint32_t ino;
    uint8_t type;
    char name[DENTRY_NAME_LEN + 1];
    struct dcache_entry *next;
} dcache_entry_t;

#define DENTRY_CACHE_BUCKETS 128
static dcache_entry_t *dcache_table[DENTRY_CACHE_BUCKETS];

int page_cache_policy = PAGE_CACHE_WRITE_BACK;
int write_back_freq = 30;

static uint8_t io_block[BLOCK_SIZE];
static uint8_t io_block2[BLOCK_SIZE];

static inline uint32_t block_to_sector(uint32_t block)
{
    return FS_START_SECTOR + block * SECTORS_PER_BLOCK;
}

static int fs_raw_read_block(uint32_t block, void *buf)
{
    uint32_t sector = block_to_sector(block);
    return bios_sd_read((unsigned)kva2pa((uintptr_t)buf), SECTORS_PER_BLOCK, sector);
}

static int fs_raw_write_block(uint32_t block, const void *buf)
{
    uint32_t sector = block_to_sector(block);
    return bios_sd_write((unsigned)kva2pa((uintptr_t)buf), SECTORS_PER_BLOCK, sector);
}

static uint32_t cache_hash(uint32_t block)
{
    return block % CACHE_BUCKETS;
}

static cache_entry_t *cache_find(uint32_t block)
{
    uint32_t idx = cache_hash(block);
    cache_entry_t *cur = cache_table[idx];
    while (cur) {
        if (cur->block == block) {
            return cur;
        }
        cur = cur->next;
    }
    return NULL;
}

static cache_entry_t *cache_get(uint32_t block)
{
    cache_entry_t *entry = cache_find(block);
    if (entry) {
        return entry;
    }
    entry = (cache_entry_t *)kmalloc(sizeof(cache_entry_t));
    if (!entry) {
        return NULL;
    }
    entry->data = (uint8_t *)kmalloc(BLOCK_SIZE);
    if (!entry->data) {
        return NULL;
    }
    if (fs_raw_read_block(block, entry->data) != 0) {
        memset(entry->data, 0, BLOCK_SIZE);
    }
    entry->block = block;
    entry->dirty = 0;
    uint32_t idx = cache_hash(block);
    entry->next = cache_table[idx];
    cache_table[idx] = entry;
    return entry;
}

static void cache_flush_entry(cache_entry_t *entry)
{
    if (entry && entry->dirty) {
        fs_raw_write_block(entry->block, entry->data);
        entry->dirty = 0;
    }
}

static void cache_flush_all(void)
{
    for (int i = 0; i < CACHE_BUCKETS; i++) {
        cache_entry_t *cur = cache_table[i];
        while (cur) {
            cache_flush_entry(cur);
            cur = cur->next;
        }
    }
    cache_last_flush = get_timer();
}

static void cache_flush_if_needed(void)
{
    if (page_cache_policy != PAGE_CACHE_WRITE_BACK || write_back_freq <= 0) {
        return;
    }
    uint64_t now = get_timer();
    if (cache_last_flush == 0) {
        cache_last_flush = now;
        return;
    }
    if (now - cache_last_flush >= (uint64_t)write_back_freq) {
        cache_flush_all();
    }
}

static void cache_clear(void)
{
    for (int i = 0; i < CACHE_BUCKETS; i++) {
        cache_table[i] = NULL;
    }
    cache_last_flush = 0;
}

static int fs_read_block(uint32_t block, void *buf)
{
    cache_flush_if_needed();
    cache_entry_t *entry = cache_get(block);
    if (!entry) {
        return -1;
    }
    memcpy(buf, entry->data, BLOCK_SIZE);
    return 0;
}

static int fs_write_block(uint32_t block, const void *buf)
{
    cache_flush_if_needed();
    cache_entry_t *entry = cache_get(block);
    if (!entry) {
        return -1;
    }
    memcpy(entry->data, buf, BLOCK_SIZE);
    entry->dirty = 1;
    if (page_cache_policy == PAGE_CACHE_WRITE_THROUGH) {
        cache_flush_entry(entry);
    }
    return 0;
}

static inline int bitmap_test(uint8_t *bitmap, uint32_t idx)
{
    return bitmap[idx / 8] & (1u << (idx % 8));
}

static inline void bitmap_set(uint8_t *bitmap, uint32_t idx)
{
    bitmap[idx / 8] |= (1u << (idx % 8));
}

static inline void bitmap_clear(uint8_t *bitmap, uint32_t idx)
{
    bitmap[idx / 8] &= ~(1u << (idx % 8));
}

static int bitmap_alloc(uint8_t *bitmap, uint32_t max)
{
    for (uint32_t i = 0; i < max; i++) {
        if (!bitmap_test(bitmap, i)) {
            bitmap_set(bitmap, i);
            return (int)i;
        }
    }
    return -1;
}

static inode_t *inode_get(uint32_t ino)
{
    if (ino >= FS_INODE_NUM) {
        return NULL;
    }
    return &inode_table[ino];
}

static void inode_reset(inode_t *inode)
{
    memset(inode, 0, sizeof(*inode));
}

static void fs_sync_superblock(void)
{
    memset(io_block, 0, sizeof(io_block));
    memcpy((uint8_t *)io_block, (const uint8_t *)&superblock, sizeof(superblock));
    fs_write_block(SUPERBLOCK_BLOCK, io_block);
}

static void fs_sync_inode_bitmap(void)
{
    uint32_t total_bytes = (FS_INODE_NUM + 7) / 8;
    for (uint32_t i = 0; i < INODE_BITMAP_BLOCKS; i++) {
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t remain = total_bytes > offset ? total_bytes - offset : 0;
        uint32_t copy = remain > BLOCK_SIZE ? BLOCK_SIZE : remain;
        memset(io_block, 0, sizeof(io_block));
        if (copy > 0) {
            memcpy(io_block, inode_bitmap + offset, copy);
        }
        fs_write_block(INODE_BITMAP_START + i, io_block);
    }
}

static void fs_sync_block_bitmap(void)
{
    uint32_t total_bytes = (FS_TOTAL_BLOCKS + 7) / 8;
    for (uint32_t i = 0; i < BLOCK_BITMAP_BLOCKS; i++) {
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t remain = total_bytes > offset ? total_bytes - offset : 0;
        uint32_t copy = remain > BLOCK_SIZE ? BLOCK_SIZE : remain;
        memset(io_block, 0, sizeof(io_block));
        if (copy > 0) {
            memcpy(io_block, block_bitmap + offset, copy);
        }
        fs_write_block(BLOCK_BITMAP_START + i, io_block);
    }
}

static void fs_sync_inode_table(void)
{
    uint32_t total_bytes = FS_INODE_NUM * sizeof(inode_t);
    uint8_t *table_bytes = (uint8_t *)inode_table;
    for (uint32_t i = 0; i < INODE_TABLE_BLOCKS; i++) {
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t remain = total_bytes > offset ? total_bytes - offset : 0;
        uint32_t copy = remain > BLOCK_SIZE ? BLOCK_SIZE : remain;
        memset(io_block, 0, sizeof(io_block));
        if (copy > 0) {
            memcpy(io_block, table_bytes + offset, copy);
        }
        fs_write_block(INODE_TABLE_START + i, io_block);
    }
}

static void fs_sync_metadata(void)
{
    fs_sync_superblock();
    fs_sync_inode_bitmap();
    fs_sync_block_bitmap();
    fs_sync_inode_table();
}

static void fs_load_inode_bitmap(void)
{
    uint32_t total_bytes = (FS_INODE_NUM + 7) / 8;
    for (uint32_t i = 0; i < INODE_BITMAP_BLOCKS; i++) {
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t remain = total_bytes > offset ? total_bytes - offset : 0;
        uint32_t copy = remain > BLOCK_SIZE ? BLOCK_SIZE : remain;
        fs_read_block(INODE_BITMAP_START + i, io_block);
        if (copy > 0) {
            memcpy(inode_bitmap + offset, io_block, copy);
        }
    }
}

static void fs_load_block_bitmap(void)
{
    uint32_t total_bytes = (FS_TOTAL_BLOCKS + 7) / 8;
    for (uint32_t i = 0; i < BLOCK_BITMAP_BLOCKS; i++) {
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t remain = total_bytes > offset ? total_bytes - offset : 0;
        uint32_t copy = remain > BLOCK_SIZE ? BLOCK_SIZE : remain;
        fs_read_block(BLOCK_BITMAP_START + i, io_block);
        if (copy > 0) {
            memcpy(block_bitmap + offset, io_block, copy);
        }
    }
}

static void fs_load_inode_table(void)
{
    uint32_t total_bytes = FS_INODE_NUM * sizeof(inode_t);
    uint8_t *table_bytes = (uint8_t *)inode_table;
    for (uint32_t i = 0; i < INODE_TABLE_BLOCKS; i++) {
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t remain = total_bytes > offset ? total_bytes - offset : 0;
        uint32_t copy = remain > BLOCK_SIZE ? BLOCK_SIZE : remain;
        fs_read_block(INODE_TABLE_START + i, io_block);
        if (copy > 0) {
            memcpy(table_bytes + offset, io_block, copy);
        }
    }
}

static int alloc_inode(void)
{
    return bitmap_alloc(inode_bitmap, FS_INODE_NUM);
}

static int alloc_block(void)
{
    return bitmap_alloc(block_bitmap, FS_TOTAL_BLOCKS);
}

static void free_block(uint32_t block)
{
    if (block >= DATA_BLOCK_START && block < FS_TOTAL_BLOCKS) {
        bitmap_clear(block_bitmap, block);
    }
}

static int inode_get_block(inode_t *inode, uint32_t file_block, int alloc)
{
    if (file_block < INODE_DIRECT_COUNT) {
        if (inode->direct[file_block] == 0 && alloc) {
            int block = alloc_block();
            if (block < 0) {
                return -1;
            }
            inode->direct[file_block] = (uint32_t)block;
            memset(io_block, 0, sizeof(io_block));
            fs_write_block((uint32_t)block, io_block);
        }
        return inode->direct[file_block] ? (int)inode->direct[file_block] : -1;
    }

    uint32_t index = file_block - INODE_DIRECT_COUNT;
    uint32_t l1 = index / INODE_INDIRECT_COUNT;
    uint32_t l2 = index % INODE_INDIRECT_COUNT;
    if (l1 >= INODE_INDIRECT_COUNT) {
        return -1;
    }

    if (inode->indirect == 0) {
        if (!alloc) {
            return -1;
        }
        int block = alloc_block();
        if (block < 0) {
            return -1;
        }
        inode->indirect = (uint32_t)block;
        memset(io_block, 0, sizeof(io_block));
        fs_write_block((uint32_t)block, io_block);
    }

    fs_read_block(inode->indirect, io_block);
    uint32_t *level1 = (uint32_t *)io_block;
    if (level1[l1] == 0) {
        if (!alloc) {
            return -1;
        }
        int block = alloc_block();
        if (block < 0) {
            return -1;
        }
        level1[l1] = (uint32_t)block;
        fs_write_block(inode->indirect, io_block);
        memset(io_block2, 0, sizeof(io_block2));
        fs_write_block((uint32_t)block, io_block2);
    }

    fs_read_block(level1[l1], io_block2);
    uint32_t *level2 = (uint32_t *)io_block2;
    if (level2[l2] == 0 && alloc) {
        int block = alloc_block();
        if (block < 0) {
            return -1;
        }
        level2[l2] = (uint32_t)block;
        fs_write_block(level1[l1], io_block2);
        memset(io_block, 0, sizeof(io_block));
        fs_write_block((uint32_t)block, io_block);
    }

    return level2[l2] ? (int)level2[l2] : -1;
}

static int file_read(inode_t *inode, uint32_t offset, void *buf, uint32_t len)
{
    if (offset >= inode->size) {
        return 0;
    }
    uint32_t remaining = inode->size - offset;
    if (len < remaining) {
        remaining = len;
    }

    uint8_t *dst = (uint8_t *)buf;
    uint32_t read_bytes = 0;
    while (remaining > 0) {
        uint32_t file_block = offset / BLOCK_SIZE;
        uint32_t block_off = offset % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - block_off;
        if (chunk > remaining) {
            chunk = remaining;
        }
        int block = inode_get_block(inode, file_block, 0);
        if (block < 0) {
            memset(dst, 0, chunk);
        } else {
            fs_read_block((uint32_t)block, io_block);
            memcpy(dst, io_block + block_off, chunk);
        }
        dst += chunk;
        offset += chunk;
        read_bytes += chunk;
        remaining -= chunk;
    }

    return (int)read_bytes;
}

static int file_write(inode_t *inode, uint32_t offset, const void *buf, uint32_t len)
{
    const uint8_t *src = (const uint8_t *)buf;
    uint32_t written = 0;
    uint32_t start = offset;
    while (written < len) {
        uint32_t file_block = offset / BLOCK_SIZE;
        uint32_t block_off = offset % BLOCK_SIZE;
        uint32_t chunk = BLOCK_SIZE - block_off;
        if (chunk > (len - written)) {
            chunk = len - written;
        }
        int block = inode_get_block(inode, file_block, 1);
        if (block < 0) {
            break;
        }
        if (chunk != BLOCK_SIZE) {
            fs_read_block((uint32_t)block, io_block);
        } else {
            memset(io_block, 0, sizeof(io_block));
        }
        memcpy(io_block + block_off, src + written, chunk);
        fs_write_block((uint32_t)block, io_block);
        offset += chunk;
        written += chunk;
    }

    if (start + written > inode->size) {
        inode->size = start + written;
    }

    return (int)written;
}

static void free_inode_blocks(inode_t *inode)
{
    for (int i = 0; i < INODE_DIRECT_COUNT; i++) {
        if (inode->direct[i]) {
            free_block(inode->direct[i]);
            inode->direct[i] = 0;
        }
    }

    if (inode->indirect) {
        fs_read_block(inode->indirect, io_block);
        uint32_t *level1 = (uint32_t *)io_block;
        for (uint32_t i = 0; i < INODE_INDIRECT_COUNT; i++) {
            if (!level1[i]) {
                continue;
            }
            fs_read_block(level1[i], io_block2);
            uint32_t *level2 = (uint32_t *)io_block2;
            for (uint32_t j = 0; j < INODE_INDIRECT_COUNT; j++) {
                if (level2[j]) {
                    free_block(level2[j]);
                }
            }
            free_block(level1[i]);
        }
        free_block(inode->indirect);
        inode->indirect = 0;
    }
}

static uint32_t dcache_hash(uint32_t parent, const char *name)
{
    uint32_t h = parent;
    for (int i = 0; name[i] != '\0'; i++) {
        h = h * 131u + (uint8_t)name[i];
    }
    return h % DENTRY_CACHE_BUCKETS;
}

static int dcache_lookup(uint32_t parent, const char *name, uint32_t *ino_out, uint8_t *type_out)
{
    uint32_t idx = dcache_hash(parent, name);
    dcache_entry_t *cur = dcache_table[idx];
    while (cur) {
        if (cur->parent == parent && strcmp(cur->name, name) == 0) {
            if (ino_out) {
                *ino_out = cur->ino;
            }
            if (type_out) {
                *type_out = cur->type;
            }
            return 0;
        }
        cur = cur->next;
    }
    return -1;
}

static void dcache_insert(uint32_t parent, const char *name, uint32_t ino, uint8_t type)
{
    uint32_t idx = dcache_hash(parent, name);
    dcache_entry_t *cur = dcache_table[idx];
    while (cur) {
        if (cur->parent == parent && strcmp(cur->name, name) == 0) {
            cur->ino = ino;
            cur->type = type;
            return;
        }
        cur = cur->next;
    }
    dcache_entry_t *entry = (dcache_entry_t *)kmalloc(sizeof(dcache_entry_t));
    if (!entry) {
        return;
    }
    entry->parent = parent;
    entry->ino = ino;
    entry->type = type;
    strcpy(entry->name, name);
    entry->next = dcache_table[idx];
    dcache_table[idx] = entry;
}

static void dcache_remove(uint32_t parent, const char *name)
{
    uint32_t idx = dcache_hash(parent, name);
    dcache_entry_t *cur = dcache_table[idx];
    dcache_entry_t *prev = NULL;
    while (cur) {
        if (cur->parent == parent && strcmp(cur->name, name) == 0) {
            if (prev) {
                prev->next = cur->next;
            } else {
                dcache_table[idx] = cur->next;
            }
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

static void dcache_clear(void)
{
    for (int i = 0; i < DENTRY_CACHE_BUCKETS; i++) {
        dcache_table[i] = NULL;
    }
}

static int dir_lookup(uint32_t dir_ino, const char *name, uint32_t *ino_out, uint8_t *type_out)
{
    inode_t *dir = inode_get(dir_ino);
    if (!dir || dir->type != INODE_TYPE_DIR) {
        return -1;
    }

    uint32_t cached_ino = 0;
    uint8_t cached_type = 0;
    if (dcache_lookup(dir_ino, name, &cached_ino, &cached_type) == 0) {
        if (ino_out) {
            *ino_out = cached_ino;
        }
        if (type_out) {
            *type_out = cached_type;
        }
        return 0;
    }

    dentry_t entry;
    uint32_t offset = 0;
    while (offset + sizeof(dentry_t) <= dir->size) {
        if (file_read(dir, offset, &entry, sizeof(entry)) != sizeof(entry)) {
            return -1;
        }
        if (entry.ino != 0 && strcmp(entry.name, name) == 0) {
            if (ino_out) {
                *ino_out = entry.ino;
            }
            if (type_out) {
                *type_out = entry.type;
            }
            dcache_insert(dir_ino, name, entry.ino, entry.type);
            return 0;
        }
        offset += sizeof(dentry_t);
    }

    return -1;
}

static int dir_add_entry(uint32_t dir_ino, const char *name, uint32_t ino, uint8_t type)
{
    inode_t *dir = inode_get(dir_ino);
    if (!dir || dir->type != INODE_TYPE_DIR) {
        return -1;
    }

    if (strlen(name) == 0 || strlen(name) > DENTRY_NAME_LEN) {
        return -1;
    }

    if (dir_lookup(dir_ino, name, NULL, NULL) == 0) {
        return -1;
    }

    dentry_t entry;
    uint32_t offset = 0;
    while (offset + sizeof(dentry_t) <= dir->size) {
        if (file_read(dir, offset, &entry, sizeof(entry)) != sizeof(entry)) {
            return -1;
        }
        if (entry.ino == 0) {
            break;
        }
        offset += sizeof(dentry_t);
    }

    memset(&entry, 0, sizeof(entry));
    entry.ino = ino;
    entry.type = type;
    strcpy(entry.name, name);

    if (file_write(dir, offset, &entry, sizeof(entry)) != sizeof(entry)) {
        return -1;
    }
    dcache_insert(dir_ino, name, ino, type);
    return 0;
}

static int dir_remove_entry(uint32_t dir_ino, const char *name, uint32_t *ino_out, uint8_t *type_out)
{
    inode_t *dir = inode_get(dir_ino);
    if (!dir || dir->type != INODE_TYPE_DIR) {
        return -1;
    }

    dentry_t entry;
    uint32_t offset = 0;
    while (offset + sizeof(dentry_t) <= dir->size) {
        if (file_read(dir, offset, &entry, sizeof(entry)) != sizeof(entry)) {
            return -1;
        }
        if (entry.ino != 0 && strcmp(entry.name, name) == 0) {
            if (ino_out) {
                *ino_out = entry.ino;
            }
            if (type_out) {
                *type_out = entry.type;
            }
            entry.ino = 0;
            memset(entry.name, 0, sizeof(entry.name));
            entry.type = 0;
            dcache_remove(dir_ino, name);
            return file_write(dir, offset, &entry, sizeof(entry)) == sizeof(entry) ? 0 : -1;
        }
        offset += sizeof(dentry_t);
    }

    return -1;
}

static int dir_is_empty(uint32_t dir_ino)
{
    inode_t *dir = inode_get(dir_ino);
    if (!dir || dir->type != INODE_TYPE_DIR) {
        return 0;
    }

    dentry_t entry;
    uint32_t offset = 0;
    while (offset + sizeof(dentry_t) <= dir->size) {
        if (file_read(dir, offset, &entry, sizeof(entry)) != sizeof(entry)) {
            return 0;
        }
        if (entry.ino != 0 && strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0) {
            return 0;
        }
        offset += sizeof(dentry_t);
    }

    return 1;
}

static int dir_get_parent(uint32_t dir_ino)
{
    inode_t *dir = inode_get(dir_ino);
    if (!dir || dir->type != INODE_TYPE_DIR) {
        return -1;
    }

    dentry_t entry;
    if (file_read(dir, sizeof(dentry_t), &entry, sizeof(entry)) != sizeof(entry)) {
        return -1;
    }
    return (int)entry.ino;
}

static int path_resolve(const char *path, uint32_t *ino_out)
{
    if (!path || path[0] == '\0') {
        *ino_out = current_running->cwd_ino;
        return 0;
    }

    uint32_t cur = (path[0] == '/') ? ROOT_INO : current_running->cwd_ino;
    const char *p = path;
    char name[DENTRY_NAME_LEN + 1];

    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            break;
        }
        int len = 0;
        while (*p != '/' && *p != '\0' && len < DENTRY_NAME_LEN) {
            name[len++] = *p++;
        }
        name[len] = '\0';

        if (strcmp(name, ".") == 0) {
            continue;
        }
        if (strcmp(name, "..") == 0) {
            int parent = dir_get_parent(cur);
            if (parent >= 0) {
                cur = (uint32_t)parent;
            }
            continue;
        }

        uint32_t next = 0;
        if (dir_lookup(cur, name, &next, NULL) != 0) {
            return -1;
        }
        cur = next;
    }

    *ino_out = cur;
    return 0;
}

static int path_parent(const char *path, uint32_t *parent_out, char *name_out)
{
    if (!path || path[0] == '\0') {
        return -1;
    }

    uint32_t cur = (path[0] == '/') ? ROOT_INO : current_running->cwd_ino;
    const char *p = path;
    char name[DENTRY_NAME_LEN + 1];

    while (*p) {
        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            return -1;
        }
        int len = 0;
        while (*p != '/' && *p != '\0' && len < DENTRY_NAME_LEN) {
            name[len++] = *p++;
        }
        name[len] = '\0';

        while (*p == '/') {
            p++;
        }
        if (*p == '\0') {
            strcpy(name_out, name);
            *parent_out = cur;
            return 0;
        }

        if (strcmp(name, ".") == 0) {
            continue;
        }
        if (strcmp(name, "..") == 0) {
            int parent = dir_get_parent(cur);
            if (parent >= 0) {
                cur = (uint32_t)parent;
            }
            continue;
        }

        uint32_t next = 0;
        if (dir_lookup(cur, name, &next, NULL) != 0) {
            return -1;
        }
        cur = next;
    }

    return -1;
}

static void fs_init_structures(void)
{
    if (inode_bitmap == NULL) {
        inode_bitmap = (uint8_t *)kmalloc((FS_INODE_NUM + 7) / 8);
    }
    if (block_bitmap == NULL) {
        block_bitmap = (uint8_t *)kmalloc((FS_TOTAL_BLOCKS + 7) / 8);
    }
    if (inode_table == NULL) {
        inode_table = (inode_t *)kmalloc(sizeof(inode_t) * FS_INODE_NUM);
    }
    memset(inode_bitmap, 0, (FS_INODE_NUM + 7) / 8);
    memset(block_bitmap, 0, (FS_TOTAL_BLOCKS + 7) / 8);
    memset(inode_table, 0, sizeof(inode_t) * FS_INODE_NUM);
    memset(fdesc_array, 0, sizeof(fdesc_array));
    cache_clear();
    dcache_clear();
}

static void fs_init_root(void)
{
    inode_t *root = inode_get(ROOT_INO);
    root->type = INODE_TYPE_DIR;
    root->links = 2;
    root->flags = 0;
    root->size = 0;

    int block = alloc_block();
    if (block >= 0) {
        root->direct[0] = (uint32_t)block;
        dentry_t entries[2];
        memset(entries, 0, sizeof(entries));
        entries[0].ino = ROOT_INO;
        entries[0].type = INODE_TYPE_DIR;
        strcpy(entries[0].name, ".");
        entries[1].ino = ROOT_INO;
        entries[1].type = INODE_TYPE_DIR;
        strcpy(entries[1].name, "..");
        memset(io_block, 0, sizeof(io_block));
        memcpy((uint8_t *)io_block, (const uint8_t *)entries, sizeof(entries));
        fs_write_block((uint32_t)block, io_block);
        root->size = sizeof(entries);
    }
}

static int str_contains(const char *str, const char *pat)
{
    int pat_len = strlen(pat);
    if (pat_len == 0) {
        return 1;
    }
    for (int i = 0; str[i] != '\0'; i++) {
        if (strncmp(&str[i], pat, (uint32_t)pat_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_int(const char *str)
{
    int value = 0;
    int seen = 0;
    for (int i = 0; str[i] != '\0'; i++) {
        if (str[i] >= '0' && str[i] <= '9') {
            seen = 1;
            value = value * 10 + (str[i] - '0');
        } else if (seen) {
            break;
        }
    }
    return seen ? value : -1;
}

static void fs_apply_vm_config(char *buf)
{
    int new_policy = page_cache_policy;
    int new_freq = write_back_freq;

    char *line = buf;
    while (*line) {
        char *next = line;
        while (*next && *next != '\n') {
            next++;
        }
        if (*next == '\n') {
            *next++ = '\0';
        }

        if (strncmp(line, "page_cache_policy", 17) == 0) {
            if (str_contains(line, "write through")) {
                new_policy = PAGE_CACHE_WRITE_THROUGH;
            } else if (str_contains(line, "write back")) {
                new_policy = PAGE_CACHE_WRITE_BACK;
            }
        } else if (strncmp(line, "write_back_freq", 15) == 0) {
            int val = parse_int(line);
            if (val > 0) {
                new_freq = val;
            }
        }
        line = next;
    }

    if (page_cache_policy == PAGE_CACHE_WRITE_BACK &&
        new_policy == PAGE_CACHE_WRITE_THROUGH) {
        cache_flush_all();
    }
    page_cache_policy = new_policy;
    write_back_freq = new_freq;
}

static void fs_load_vm_config(void)
{
    if (superblock.vm_inode == 0) {
        return;
    }
    inode_t *inode = inode_get(superblock.vm_inode);
    if (!inode) {
        return;
    }
    inode->flags |= INODE_FLAG_VM;
    uint32_t size = inode->size;
    if (size == 0) {
        return;
    }
    char buf[128];
    if (size >= sizeof(buf)) {
        size = sizeof(buf) - 1;
    }
    if (file_read(inode, 0, buf, size) <= 0) {
        return;
    }
    buf[size] = '\0';
    fs_apply_vm_config(buf);
}

static void fs_create_vm_file(void)
{
    const char *content = "page_cache_policy = write back\nwrite_back_freq = 30\n";

    do_mkdir("/proc");
    do_mkdir("/proc/sys");

    int fd = do_open("/proc/sys/vm", O_WRONLY);
    if (fd < 0) {
        return;
    }
    do_write(fd, (char *)content, (int)strlen(content));
    do_close(fd);

    uint32_t ino = 0;
    if (path_resolve("/proc/sys/vm", &ino) == 0) {
        inode_t *inode = inode_get(ino);
        if (inode) {
            inode->flags |= INODE_FLAG_VM;
        }
        superblock.vm_inode = ino;
        fs_sync_inode_table();
        fs_sync_superblock();
    }
}

void fs_init(void)
{
    if (fs_ready) {
        return;
    }

    fs_init_structures();
    fs_read_block(SUPERBLOCK_BLOCK, io_block);
    memcpy((uint8_t *)&superblock, (const uint8_t *)io_block, sizeof(superblock));

    if (superblock.magic != SUPERBLOCK_MAGIC) {
        do_mkfs();
        fs_ready = 1;
        return;
    }

    fs_load_inode_bitmap();
    fs_load_block_bitmap();
    fs_load_inode_table();
    if (superblock.vm_inode == 0) {
        fs_create_vm_file();
    }
    fs_load_vm_config();
    fs_ready = 1;
}

int do_mkfs(void)
{
    fs_init_structures();
    page_cache_policy = PAGE_CACHE_WRITE_BACK;
    write_back_freq = 30;

    superblock.magic = SUPERBLOCK_MAGIC;
    superblock.block_size = BLOCK_SIZE;
    superblock.fs_start_sector = FS_START_SECTOR;
    superblock.fs_total_sectors = FS_TOTAL_SECTORS;
    superblock.total_blocks = FS_TOTAL_BLOCKS;
    superblock.inode_count = FS_INODE_NUM;
    superblock.inode_bitmap_start = INODE_BITMAP_START;
    superblock.block_bitmap_start = BLOCK_BITMAP_START;
    superblock.inode_table_start = INODE_TABLE_START;
    superblock.data_block_start = DATA_BLOCK_START;
    superblock.root_inode = ROOT_INO;
    superblock.vm_inode = 0;

    for (uint32_t i = 0; i < DATA_BLOCK_START; i++) {
        bitmap_set(block_bitmap, i);
    }

    bitmap_set(inode_bitmap, ROOT_INO);
    fs_init_root();
    fs_sync_metadata();
    fs_create_vm_file();
    cache_flush_all();

    printu("[mkfs] fs_size=%uMB, start_sector=0x%x\n", (FS_TOTAL_SECTORS * SECTOR_SIZE) / (1024 * 1024), FS_START_SECTOR);
    printu("[mkfs] inode_bitmap_block=%u, block_bitmap_block=%u\n", INODE_BITMAP_START, BLOCK_BITMAP_START);
    printu("[mkfs] inode_table_block=%u, data_block_start=%u\n", INODE_TABLE_START, DATA_BLOCK_START);

    fs_ready = 1;
    return 0;
}

int do_statfs(void)
{
    if (!fs_ready) {
        fs_init();
    }

    uint32_t used_blocks = 0;
    uint32_t used_inodes = 0;

    for (uint32_t i = 0; i < FS_TOTAL_BLOCKS; i++) {
        if (bitmap_test(block_bitmap, i)) {
            used_blocks++;
        }
    }

    for (uint32_t i = 0; i < FS_INODE_NUM; i++) {
        if (bitmap_test(inode_bitmap, i)) {
            used_inodes++;
        }
    }

    printu("[statfs] total_blocks=%u, used_blocks=%u, free_blocks=%u\n",
           FS_TOTAL_BLOCKS, used_blocks, FS_TOTAL_BLOCKS - used_blocks);
    printu("[statfs] total_inodes=%u, used_inodes=%u, free_inodes=%u\n",
           FS_INODE_NUM, used_inodes, FS_INODE_NUM - used_inodes);

    return 0;
}

int do_cd(char *path)
{
    uint32_t ino = 0;
    if (path_resolve(path, &ino) != 0) {
        return -1;
    }
    inode_t *inode = inode_get(ino);
    if (!inode || inode->type != INODE_TYPE_DIR) {
        return -1;
    }
    current_running->cwd_ino = ino;
    return 0;
}

int do_mkdir(char *path)
{
    uint32_t parent = 0;
    char name[DENTRY_NAME_LEN + 1];
    if (path_parent(path, &parent, name) != 0) {
        return -1;
    }

    if (dir_lookup(parent, name, NULL, NULL) == 0) {
        return -1;
    }

    int ino = alloc_inode();
    if (ino < 0) {
        return -1;
    }

    inode_t *inode = inode_get((uint32_t)ino);
    inode_reset(inode);
    inode->type = INODE_TYPE_DIR;
    inode->links = 2;
    inode->flags = 0;
    inode->size = 0;

    int block = alloc_block();
    if (block < 0) {
        return -1;
    }
    inode->direct[0] = (uint32_t)block;

    dentry_t entries[2];
    memset(entries, 0, sizeof(entries));
    entries[0].ino = (uint32_t)ino;
    entries[0].type = INODE_TYPE_DIR;
    strcpy(entries[0].name, ".");
    entries[1].ino = parent;
    entries[1].type = INODE_TYPE_DIR;
    strcpy(entries[1].name, "..");
    memset(io_block, 0, sizeof(io_block));
    memcpy((uint8_t *)io_block, (const uint8_t *)entries, sizeof(entries));
    fs_write_block((uint32_t)block, io_block);
    inode->size = sizeof(entries);

    inode_t *parent_inode = inode_get(parent);
    parent_inode->links++;
    if (dir_add_entry(parent, name, (uint32_t)ino, INODE_TYPE_DIR) != 0) {
        return -1;
    }

    fs_sync_metadata();
    return 0;
}

int do_rmdir(char *path)
{
    uint32_t parent = 0;
    char name[DENTRY_NAME_LEN + 1];
    if (path_parent(path, &parent, name) != 0) {
        return -1;
    }

    uint32_t ino = 0;
    uint8_t type = 0;
    if (dir_lookup(parent, name, &ino, &type) != 0 || type != INODE_TYPE_DIR) {
        return -1;
    }

    if (!dir_is_empty(ino)) {
        return -1;
    }

    if (dir_remove_entry(parent, name, NULL, NULL) != 0) {
        return -1;
    }

    inode_t *inode = inode_get(ino);
    free_inode_blocks(inode);
    inode_reset(inode);
    bitmap_clear(inode_bitmap, ino);

    inode_t *parent_inode = inode_get(parent);
    if (parent_inode->links > 2) {
        parent_inode->links--;
    }

    fs_sync_metadata();
    return 0;
}

int do_ls(char *path, int option)
{
    uint32_t ino = 0;
    if (path_resolve(path, &ino) != 0) {
        return -1;
    }

    inode_t *inode = inode_get(ino);
    if (!inode) {
        return -1;
    }

    if (inode->type != INODE_TYPE_DIR) {
        if (option) {
            printu("%u %u %u %s\n", ino, inode->links, inode->size, path);
        } else {
            printu("%s\n", path);
        }
        return 0;
    }

    dentry_t entry;
    uint32_t offset = 0;
    while (offset + sizeof(dentry_t) <= inode->size) {
        if (file_read(inode, offset, &entry, sizeof(entry)) != sizeof(entry)) {
            break;
        }
        if (entry.ino != 0 && strcmp(entry.name, ".") != 0 && strcmp(entry.name, "..") != 0) {
            inode_t *child = inode_get(entry.ino);
            if (option) {
                uint32_t size = child ? child->size : 0;
                uint16_t links = child ? child->links : 0;
                printu("%u %u %u %s\n", entry.ino, links, size, entry.name);
            } else {
                printu("%s ", entry.name);
            }
        }
        offset += sizeof(dentry_t);
    }
    if (!option) {
        printu("\n");
    }

    return 0;
}

static fdesc_t *fdesc_get(int fd)
{
    if (fd < 0 || fd >= NUM_FDESCS) {
        return NULL;
    }
    fdesc_t *desc = &fdesc_array[fd];
    if (!desc->used || desc->owner != current_running->pid) {
        return NULL;
    }
    return desc;
}

static int fdesc_alloc(int mode, uint32_t ino)
{
    for (int i = 0; i < NUM_FDESCS; i++) {
        if (!fdesc_array[i].used) {
            fdesc_array[i].used = 1;
            fdesc_array[i].mode = mode;
            fdesc_array[i].ino = ino;
            fdesc_array[i].offset = 0;
            fdesc_array[i].owner = current_running->pid;
            return i;
        }
    }
    return -1;
}

int do_open(char *path, int mode)
{
    uint32_t ino = 0;
    if (path_resolve(path, &ino) != 0) {
        if (!(mode & O_WRONLY)) {
            return -1;
        }
        uint32_t parent = 0;
        char name[DENTRY_NAME_LEN + 1];
        if (path_parent(path, &parent, name) != 0) {
            return -1;
        }
        int new_ino = alloc_inode();
        if (new_ino < 0) {
            return -1;
        }
        inode_t *inode = inode_get((uint32_t)new_ino);
        inode_reset(inode);
        inode->type = INODE_TYPE_FILE;
        inode->links = 1;
        inode->flags = 0;
        inode->size = 0;
        if (dir_add_entry(parent, name, (uint32_t)new_ino, INODE_TYPE_FILE) != 0) {
            return -1;
        }
        fs_sync_metadata();
        return fdesc_alloc(mode, (uint32_t)new_ino);
    }

    inode_t *inode = inode_get(ino);
    if (!inode || inode->type != INODE_TYPE_FILE) {
        return -1;
    }
    return fdesc_alloc(mode, ino);
}

int do_read(int fd, char *buff, int length)
{
    fdesc_t *desc = fdesc_get(fd);
    if (!desc || !(desc->mode & O_RDONLY)) {
        return -1;
    }
    inode_t *inode = inode_get(desc->ino);
    if (!inode) {
        return -1;
    }
    int read_bytes = file_read(inode, desc->offset, buff, (uint32_t)length);
    if (read_bytes > 0) {
        desc->offset += (uint32_t)read_bytes;
    }
    return read_bytes;
}

int do_write(int fd, char *buff, int length)
{
    fdesc_t *desc = fdesc_get(fd);
    if (!desc || !(desc->mode & O_WRONLY)) {
        return -1;
    }
    inode_t *inode = inode_get(desc->ino);
    if (!inode) {
        return -1;
    }
    if ((inode->flags & INODE_FLAG_VM) && desc->offset == 0) {
        inode->size = 0;
    }
    int written = file_write(inode, desc->offset, buff, (uint32_t)length);
    if (written > 0) {
        desc->offset += (uint32_t)written;
        fs_sync_inode_table();
        fs_sync_block_bitmap();
        if (inode->flags & INODE_FLAG_VM) {
            fs_load_vm_config();
        }
    }
    return written;
}

int do_close(int fd)
{
    fdesc_t *desc = fdesc_get(fd);
    if (!desc) {
        return -1;
    }
    desc->used = 0;
    desc->ino = 0;
    desc->offset = 0;
    desc->mode = 0;
    desc->owner = 0;
    return 0;
}

int do_ln(char *src_path, char *dst_path)
{
    uint32_t src_ino = 0;
    if (path_resolve(src_path, &src_ino) != 0) {
        return -1;
    }
    inode_t *inode = inode_get(src_ino);
    if (!inode || inode->type != INODE_TYPE_FILE) {
        return -1;
    }
    uint32_t parent = 0;
    char name[DENTRY_NAME_LEN + 1];
    if (path_parent(dst_path, &parent, name) != 0) {
        return -1;
    }
    if (dir_add_entry(parent, name, src_ino, INODE_TYPE_FILE) != 0) {
        return -1;
    }
    inode->links++;
    fs_sync_inode_table();
    fs_sync_inode_bitmap();
    return 0;
}

int do_rm(char *path)
{
    uint32_t parent = 0;
    char name[DENTRY_NAME_LEN + 1];
    if (path_parent(path, &parent, name) != 0) {
        return -1;
    }
    uint32_t ino = 0;
    uint8_t type = 0;
    if (dir_lookup(parent, name, &ino, &type) != 0 || type != INODE_TYPE_FILE) {
        return -1;
    }
    if (dir_remove_entry(parent, name, NULL, NULL) != 0) {
        return -1;
    }
    inode_t *inode = inode_get(ino);
    if (!inode) {
        return -1;
    }
    if (inode->links > 0) {
        inode->links--;
    }
    if (inode->links == 0) {
        free_inode_blocks(inode);
        inode_reset(inode);
        bitmap_clear(inode_bitmap, ino);
        fs_sync_block_bitmap();
        fs_sync_inode_bitmap();
    }
    fs_sync_inode_table();
    return 0;
}

int do_lseek(int fd, int offset, int whence)
{
    fdesc_t *desc = fdesc_get(fd);
    if (!desc) {
        return -1;
    }
    inode_t *inode = inode_get(desc->ino);
    if (!inode) {
        return -1;
    }
    int new_offset = 0;
    if (whence == SEEK_SET) {
        new_offset = offset;
    } else if (whence == SEEK_CUR) {
        new_offset = (int)desc->offset + offset;
    } else if (whence == SEEK_END) {
        new_offset = (int)inode->size + offset;
    } else {
        return -1;
    }
    if (new_offset < 0) {
        return -1;
    }
    desc->offset = (uint32_t)new_offset;
    return new_offset;
}
