#include <os/mm.h>
#include <os/string.h>
#include <os/sched.h>
#include <printk.h>
#include <type.h>
#include <pgtable.h>
#include <common.h>

// 使用内核虚拟地址管理动态内存
static ptr_t kernMemCurr = FREEMEM_KERNEL;
static ptr_t free_list_head = 0; // 空闲链表头

// Page frame management for swap
typedef struct page_frame {
    uintptr_t va;           // Virtual address mapped to this frame
    uintptr_t pgdir;        // Page directory of the process
    int swap_idx;           // Index in swap space (-1 if not swapped)
    int reference_bit;      // For Clock algorithm
    int in_use;             // Whether this frame is allocated
    struct page_frame *next; // For Clock algorithm circular list
} page_frame_t;

static page_frame_t page_frames[TOTAL_PHYSICAL_PAGES];
static page_frame_t *clock_hand = NULL;  // Clock algorithm pointer
static int swap_bitmap[MAX_SWAP_PAGES];  // Swap space allocation bitmap
static int total_allocated_pages = 0;
static int swap_enabled = 0;

void init_swap() {
    // Initialize page frames
    for (int i = 0; i < TOTAL_PHYSICAL_PAGES; i++) {
        page_frames[i].va = 0;
        page_frames[i].pgdir = 0;
        page_frames[i].swap_idx = -1;
        page_frames[i].reference_bit = 0;
        page_frames[i].in_use = 0;
        page_frames[i].next = &page_frames[(i + 1) % TOTAL_PHYSICAL_PAGES];
    }
    clock_hand = &page_frames[0];
    
    // Initialize swap bitmap
    for (int i = 0; i < MAX_SWAP_PAGES; i++) {
        swap_bitmap[i] = 0;  // 0 = free, 1 = used
    }
    
    swap_enabled = 1;
    printk("> [SWAP] Swap mechanism initialized\n");
}

// Allocate a swap slot
static int alloc_swap_slot() {
    for (int i = 0; i < MAX_SWAP_PAGES; i++) {
        if (swap_bitmap[i] == 0) {
            swap_bitmap[i] = 1;
            return i;
        }
    }
    return -1;  // No free swap slot
}

// Free a swap slot
static void free_swap_slot(int idx) {
    if (idx >= 0 && idx < MAX_SWAP_PAGES) {
        swap_bitmap[idx] = 0;
    }
}


// Clock algorithm to select a victim page
int swap_out_page() {
    if (!swap_enabled) return -1;
    
    int attempts = 0;
    while (attempts < TOTAL_PHYSICAL_PAGES * 2) {
        if (clock_hand->in_use && clock_hand->pgdir != 0) {
            // Check if this is a user page (not kernel)
            if (clock_hand->va < 0x8000000000000000UL) {
                if (clock_hand->reference_bit == 0) {
                    // Found victim
                    page_frame_t *victim = clock_hand;
                    clock_hand = clock_hand->next;
                    
                    // Get PTE
                    uintptr_t pte_ptr = get_pteptr_of(victim->va, victim->pgdir);
                    if (!pte_ptr) {
                        victim->in_use = 0;
                        return -1;
                    }
                    
                    PTE *pte = (PTE *)pte_ptr;
                    uintptr_t pa = get_pa(*pte);
                    
                    // Allocate swap slot
                    int swap_idx = alloc_swap_slot();
                    if (swap_idx < 0) {
                        return -1;  // No swap space
                    }
                    
                    // Write page to SD card
                    uintptr_t kva = pa2kva(pa);
                    uint32_t sector = SWAP_START_SECTOR + swap_idx * 8;  // 8 sectors per page
                    sd_write(kva2pa(kva), 8, sector);
                    
                    // Update PTE: clear present bit, store swap index in software bits
                    *pte = (*pte & ~_PAGE_PRESENT) | ((uint64_t)swap_idx << _PAGE_PFN_SHIFT) | _PAGE_SOFT;
                    
                    // Flush TLB
                    local_flush_tlb_page(victim->va);
                    
                    // Free the physical page
                    freePage(kva);
                    // 不减少total_allocated_pages，因为freePage已经处理了
                    // total_allocated_pages保持不变（页面只是移到空闲链表）
                    
                    // Mark frame as free
                    victim->swap_idx = swap_idx;
                    victim->in_use = 0;
                    
                    return swap_idx;
                } else {
                    // Give second chance
                    clock_hand->reference_bit = 0;
                }
            }
        }
        clock_hand = clock_hand->next;
        attempts++;
    }
    
    return -1;  // Failed to find victim
}

// Swap in a page from SD card
void swap_in_page(uintptr_t va, uintptr_t pgdir, int swap_idx) {
    if (!swap_enabled || swap_idx < 0 || swap_idx >= MAX_SWAP_PAGES) return;
    
    // Allocate a new physical page
    ptr_t new_page = allocPage(1);
    if (new_page == 0) {
        // Out of memory, need to swap out first
        if (swap_out_page() >= 0) {
            new_page = allocPage(1);
        }
        if (new_page == 0) {
            printk("FATAL: Cannot allocate page for swap in\n");
            return;
        }
    }
    
    // Read page from SD card
    uint32_t sector = SWAP_START_SECTOR + swap_idx * 8;
    sd_read(kva2pa(new_page), 8, sector);
    
    // Update PTE
    uintptr_t pte_ptr = get_pteptr_of(va, pgdir);
    if (pte_ptr) {
        PTE *pte = (PTE *)pte_ptr;
        set_pfn(pte, kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                          _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
        local_flush_tlb_page(va);
    }
    
    // Free swap slot
    free_swap_slot(swap_idx);
    
    // Track this page frame
    for (int i = 0; i < TOTAL_PHYSICAL_PAGES; i++) {
        if (!page_frames[i].in_use) {
            page_frames[i].va = va;
            page_frames[i].pgdir = pgdir;
            page_frames[i].swap_idx = -1;
            page_frames[i].reference_bit = 1;
            page_frames[i].in_use = 1;
            total_allocated_pages++;
            break;
        }
    }
}

ptr_t allocPage(int numPage)
{
    // Always try to allocate from free list first for single pages
    if (free_list_head != 0 && numPage == 1) {
        ptr_t ret = free_list_head;
        free_list_head = *(ptr_t *)free_list_head;
        memset((void *)ret, 0, PAGE_SIZE);
        return ret;
    }

    // Check if we have enough physical pages
    if (total_allocated_pages + numPage > TOTAL_PHYSICAL_PAGES) {
        if (swap_enabled) {
            // Try to swap out pages
            while (total_allocated_pages + numPage > TOTAL_PHYSICAL_PAGES - 10) {
                if (!swap_out_page()) {
                    break;  // Can't swap more
                }

                // After swap_out, try to use freed page from free list
                if (free_list_head != 0 && numPage == 1) {
                    ptr_t ret = free_list_head;
                    free_list_head = *(ptr_t *)free_list_head;
                    memset((void *)ret, 0, PAGE_SIZE);
                    return ret;
                }
            }
        }

        // Still not enough memory
        if (total_allocated_pages + numPage > TOTAL_PHYSICAL_PAGES) {
            return 0;  // Allocation failed
        }
    }

    // Allocate from kernel memory (only when free list is empty)
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;

    // Track allocation
    total_allocated_pages += numPage;

    return ret;
}

void freePage(ptr_t baseAddr) {
    // 将释放的页插入空闲链表头部
    *(ptr_t *)baseAddr = free_list_head;
    free_list_head = baseAddr;

    // 注意：不减total_allocated_pages
    // 因为页面仍然被分配（只是在空闲链表中）
    // total_allocated_pages代表已分配的总页面数，包括空闲和使用的
}

// Get free memory in bytes
size_t get_free_memory() {
    // Count free pages from free list
    int free_pages_count = 0;
    ptr_t p = free_list_head;
    while (p != 0 && free_pages_count < TOTAL_PHYSICAL_PAGES) {
        free_pages_count++;
        p = *(ptr_t *)p;
    }

    // Add unallocated memory
    size_t unallocated_pages = 0;
    ptr_t mem_end = 0xffffffc060000000;  // End of physical memory (approximate)
    if (kernMemCurr < mem_end) {
        unallocated_pages = (mem_end - kernMemCurr) / PAGE_SIZE;
    }

    // Total free = pages in free list + unallocated pages
    size_t total_free_pages = free_pages_count + unallocated_pages;

    return total_free_pages * PAGE_SIZE;
}

// 递归或循环释放页表
void free_pgtable_pages(uintptr_t pgdir) {
    PTE *l2_pgdir = (PTE *)pgdir; // 已经是 KVA
    // 1. 遍历 L2 (根页表)
    for (int i = 0; i < NUM_PTE_ENTRY / 2; i++) {
        if (l2_pgdir[i] & _PAGE_PRESENT) {
            PTE *l1_pgdir = (PTE *)pa2kva(get_pa(l2_pgdir[i]));
            // 2. 遍历 L1
            for (int j = 0; j < NUM_PTE_ENTRY; j++) {
                if (l1_pgdir[j] & _PAGE_PRESENT) {
                    PTE *l0_pgdir = (PTE *)pa2kva(get_pa(l1_pgdir[j]));

                    // 3. 遍历 L0
                    for (int k = 0; k < 512; k++) {
                        if (l0_pgdir[k] & _PAGE_PRESENT) {
                            // 这是一个有效的用户物理页
                            // 如果设置了 _PAGE_USER，说明是用户数据页，需要回收
                            if (l0_pgdir[k] & _PAGE_USER) {
                                ptr_t page_pa = get_pa(l0_pgdir[k]);
                                freePage(pa2kva(page_pa)); // 释放数据页
                            }
                        }
                    }
                    // 释放 L0 页表本身
                    freePage((ptr_t)l0_pgdir);
                }
            }
            // 释放 L1 页表本身
            freePage((ptr_t)l1_pgdir);
        }
    }
    // 释放 L2 页表本身
    freePage(pgdir);
}

void *kmalloc(size_t size) {
    return (void *)allocPage((size + PAGE_SIZE - 1) / PAGE_SIZE);
}

void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir) {
    PTE *dest_pgdir_kva = (PTE *)pa2kva(dest_pgdir);
    PTE *src_pgdir_kva = (PTE *)pa2kva(src_pgdir);
    // 复制内核空间映射 (高地址部分)
    for (int i = 256; i < NUM_PTE_ENTRY; ++i) {
        dest_pgdir_kva[i] = src_pgdir_kva[i];
    }
}

uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir) {
    // 确保地址对齐到页边界
    va = va & ~(PAGE_SIZE - 1);
    
    PTE *pgdir_kva = (PTE *)pgdir;

    // L2 (根页表) 的索引：取 va 的 [38:30] 位
    uint64_t vpn2 = (va >> 30) & 0x1ff;
    if ((pgdir_kva[vpn2] & _PAGE_PRESENT) == 0) {
        ptr_t new_page = allocPage(1);
        clear_pgdir(new_page);
        // [修正] 中间页表项：只给 V 位，绝对不要给 U 位
        set_pfn(&pgdir_kva[vpn2], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgdir_kva[vpn2], _PAGE_PRESENT);
    }

    // 1. 获取 L1 页表的物理地址 PA，并转为 KVA
    PTE *pmd = (PTE *)pa2kva(get_pa(pgdir_kva[vpn2]));
    // 2. L1 索引：取 va 的 [29:21] 位
    uint64_t vpn1 = (va >> 21) & 0x1ff;
    if ((pmd[vpn1] & _PAGE_PRESENT) == 0) {
        ptr_t new_page = allocPage(1);
        clear_pgdir(new_page);
        // [修正] 中间页表项：只给 V 位，绝对不要给 U 位
        set_pfn(&pmd[vpn1], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
    }

    // 1. 获取 L0 页表的物理地址 PA，并转为 KVA
    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    // 2. L0 索引：取 va 的 [20:12] 位
    uint64_t vpn0 = (va >> 12) & 0x1ff;

    // 如果已经存在映射，直接返回
    if (pte[vpn0] & _PAGE_PRESENT) {
        return pa2kva(get_pa(pte[vpn0]));
    }

    // 建立最终映射
    ptr_t final_page = allocPage(1);
    // 清零新分配的页，防止访问到脏数据
    memset((void *)final_page, 0, PAGE_SIZE);
    set_pfn(&pte[vpn0], kva2pa(final_page) >> NORMAL_PAGE_SHIFT);
    // [保持] 叶子节点：必须给足 V, R, W, X, U, A, D
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);

    return final_page;
}

uintptr_t shm_page_get(int key) { return 0; }
void shm_page_dt(uintptr_t addr) {}

// Pipe management for Task 5
static pipe_t pipes[MAX_PIPES];
static int pipe_initialized = 0;

// Simple free list for pipe_page_t structures
static pipe_page_t *free_pipe_pages = NULL;

// Initialize pipe system
static void init_pipe_system() {
    if (pipe_initialized) return;

    for (int i = 0; i < MAX_PIPES; i++) {
        pipes[i].name[0] = '\0';
        pipes[i].ref_count = 0;
        pipes[i].head = NULL;
        pipes[i].tail = NULL;
        pipes[i].total_pages = 0;
        pipes[i].is_open = 0;
    }
    pipe_initialized = 1;
    printk("> [PIPE] Pipe system initialized\n");
}

// Find pipe by name, returns -1 if not found
static int find_pipe_by_name(const char *name) {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (pipes[i].is_open && strcmp(pipes[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

// Find a free pipe slot, returns -1 if none available
static int find_free_pipe_slot() {
    for (int i = 0; i < MAX_PIPES; i++) {
        if (!pipes[i].is_open) {
            return i;
        }
    }
    return -1;
}

// Close a pipe (decrement reference count and cleanup if needed)
void do_pipe_close(int pipe_idx) {
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].is_open) {
        return;
    }

    pipes[pipe_idx].ref_count--;

    // If no more references, clean up the pipe
    if (pipes[pipe_idx].ref_count <= 0) {
        // Free all pages in the pipe
        pipe_page_t *page = pipes[pipe_idx].head;
        while (page) {
            pipe_page_t *next = page->next;

            // Return page structure to free list
            page->next = free_pipe_pages;
            free_pipe_pages = page;

            page = next;
        }

        // Mark pipe as closed
        pipes[pipe_idx].is_open = 0;
        pipes[pipe_idx].head = NULL;
        pipes[pipe_idx].tail = NULL;
        pipes[pipe_idx].total_pages = 0;
        pipes[pipe_idx].ref_count = 0;
    }
}

// Create or open a pipe
int do_pipe_open(const char *name) {
    if (!pipe_initialized) {
        init_pipe_system();
    }

    if (!name || strlen(name) >= 31) {
        return -1;  // Invalid name
    }

    // Check if current process has too many pipes open
    if (current_running->num_open_pipes >= MAX_PROCESS_PIPES) {
        return -1;  // Too many pipes open
    }

    // Check if pipe already exists
    int idx = find_pipe_by_name(name);
    if (idx >= 0) {
        pipes[idx].ref_count++;

        // Track this pipe in the current process
        for (int i = 0; i < MAX_PROCESS_PIPES; i++) {
            if (current_running->open_pipes[i] == -1) {
                current_running->open_pipes[i] = idx;
                current_running->num_open_pipes++;
                break;
            }
        }

        return idx;
    }

    // Create new pipe
    idx = find_free_pipe_slot();
    if (idx < 0) {
        return -1;  // No free slots
    }

    strncpy(pipes[idx].name, name, 31);
    pipes[idx].name[31] = '\0';
    pipes[idx].ref_count = 1;
    pipes[idx].head = NULL;
    pipes[idx].tail = NULL;
    pipes[idx].total_pages = 0;
    pipes[idx].is_open = 1;

    // Track this pipe in the current process
    for (int i = 0; i < MAX_PROCESS_PIPES; i++) {
        if (current_running->open_pipes[i] == -1) {
            current_running->open_pipes[i] = idx;
            current_running->num_open_pipes++;
            break;
        }
    }

    return idx;
}

// Give pages to pipe (zero-copy implementation)
long do_pipe_give_pages(int pipe_idx, void *src, size_t length) {
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].is_open) {
        return -1;
    }

    if (!src || length == 0) {
        return -1;
    }

    // Calculate number of pages needed
    int num_pages = (length + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t src_va = (uintptr_t)src;

    // For each page, create a pipe_page entry and remap
    for (int i = 0; i < num_pages; i++) {
        // Allocate or reuse pipe_page_t structure
        pipe_page_t *page;
        if (free_pipe_pages) {
            page = free_pipe_pages;
            free_pipe_pages = page->next;
        } else {
            page = (pipe_page_t *)kmalloc(sizeof(pipe_page_t));
            if (!page) {
                return -1;  // Out of memory
            }
        }

        uintptr_t page_va = src_va + i * PAGE_SIZE;
        size_t copy_len = (i == num_pages - 1) ? (length - i * PAGE_SIZE) : PAGE_SIZE;

        // Get the physical page backing this virtual address
        uintptr_t pte_ptr = get_pteptr_of(page_va, current_running->pgdir);
        if (!pte_ptr) {
            // Page not mapped, allocate it first
            alloc_page_helper(page_va, current_running->pgdir);
            pte_ptr = get_pteptr_of(page_va, current_running->pgdir);
            if (!pte_ptr) {
                return -1;
            }
        }

        PTE *pte = (PTE *)pte_ptr;
        uintptr_t pa = get_pa(*pte);

        // Initialize pipe page structure
        page->kva = (void *)pa2kva(pa);
        page->pa = pa;
        page->in_use = 1;
        page->size = copy_len;
        page->next = NULL;

        // Add to pipe
        if (pipes[pipe_idx].tail) {
            pipes[pipe_idx].tail->next = page;
        } else {
            pipes[pipe_idx].head = page;
        }
        pipes[pipe_idx].tail = page;
        pipes[pipe_idx].total_pages++;
    }

    return length;
}

// Take pages from pipe (zero-copy implementation)
long do_pipe_take_pages(int pipe_idx, void *dst, size_t length) {
    if (pipe_idx < 0 || pipe_idx >= MAX_PIPES || !pipes[pipe_idx].is_open) {
        return -1;
    }

    if (!dst || length == 0) {
        return -1;
    }

    if (!pipes[pipe_idx].head) {
        return 0;  // No data available
    }

    uintptr_t dst_va = (uintptr_t)dst;
    size_t total_copied = 0;

    // Take pages from pipe and remap to receiver
    while (pipes[pipe_idx].head && total_copied < length) {
        pipe_page_t *page = pipes[pipe_idx].head;
        pipes[pipe_idx].head = page->next;
        if (!pipes[pipe_idx].head) {
            pipes[pipe_idx].tail = NULL;
        }
        pipes[pipe_idx].total_pages--;

        // Get the PTE for destination address
        uintptr_t pte_ptr = get_pteptr_of(dst_va + total_copied, current_running->pgdir);
        if (!pte_ptr) {
            // Page not mapped, allocate it first
            alloc_page_helper(dst_va + total_copied, current_running->pgdir);
            pte_ptr = get_pteptr_of(dst_va + total_copied, current_running->pgdir);
        }

        if (pte_ptr) {
            PTE *pte = (PTE *)pte_ptr;

            // Remap the physical page to destination
            set_pfn(pte, page->pa >> NORMAL_PAGE_SHIFT);
            set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);

            // Flush TLB for this page
            local_flush_tlb_page(dst_va + total_copied);

            total_copied += page->size;
        }

        // Return page structure to free list
        page->next = free_pipe_pages;
        free_pipe_pages = page;
    }

    return total_copied;
}