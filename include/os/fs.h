#ifndef __INCLUDE_OS_FS_H__
#define __INCLUDE_OS_FS_H__

#include <type.h>

/* macros of file system */
#define SUPERBLOCK_MAGIC 0xDF4C4459
#define NUM_FDESCS 16

#define SECTOR_SIZE 512
#define BLOCK_SIZE 4096
#define SECTORS_PER_BLOCK (BLOCK_SIZE / SECTOR_SIZE)

#define FS_START_SECTOR 0x100000  /* 512MB */
#define FS_TOTAL_SECTORS 0x100000 /* 512MB */
#define FS_TOTAL_BLOCKS (FS_TOTAL_SECTORS / SECTORS_PER_BLOCK)
#define FS_INODE_NUM 4096

#define INODE_DIRECT_COUNT 12
#define INODE_INDIRECT_COUNT (BLOCK_SIZE / sizeof(uint32_t))

#define DENTRY_NAME_LEN 27

/* inode types */
#define INODE_TYPE_FILE 1
#define INODE_TYPE_DIR 2

/* inode flags */
#define INODE_FLAG_VM 0x1

/* page cache policy */
#define PAGE_CACHE_WRITE_BACK 0
#define PAGE_CACHE_WRITE_THROUGH 1

/* data structures of file system */
typedef struct superblock {
    uint32_t magic;
    uint32_t block_size;
    uint32_t fs_start_sector;
    uint32_t fs_total_sectors;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t inode_bitmap_start;
    uint32_t block_bitmap_start;
    uint32_t inode_table_start;
    uint32_t data_block_start;
    uint32_t root_inode;
    uint32_t vm_inode;
} superblock_t;

typedef struct dentry {
    uint32_t ino;
    uint8_t type;
    char name[DENTRY_NAME_LEN];
} dentry_t;

typedef struct inode {
    uint16_t type;
    uint16_t links;
    uint16_t flags;
    uint16_t reserved;
    uint32_t size;
    uint32_t direct[INODE_DIRECT_COUNT];
    uint32_t indirect;
} inode_t;

typedef struct fdesc {
    int used;
    int mode;
    uint32_t ino;
    uint32_t offset;
    pid_t owner;
} fdesc_t;

/* cache policy state (Task 3) */
extern int page_cache_policy;
extern int write_back_freq;

/* modes of do_open */
#define O_RDONLY 1  /* read only open */
#define O_WRONLY 2  /* write only open */
#define O_RDWR   3  /* read/write open */

/* whence of do_lseek */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* fs function declarations */
extern int do_mkfs(void);
extern int do_statfs(void);
extern void fs_init(void);
extern int do_cd(char *path);
extern int do_mkdir(char *path);
extern int do_rmdir(char *path);
extern int do_ls(char *path, int option);
extern int do_open(char *path, int mode);
extern int do_read(int fd, char *buff, int length);
extern int do_write(int fd, char *buff, int length);
extern int do_close(int fd);
extern int do_ln(char *src_path, char *dst_path);
extern int do_rm(char *path);
extern int do_lseek(int fd, int offset, int whence);

#endif
