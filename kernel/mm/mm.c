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

// Simple swap tracking
#ifndef ENABLE_ALLOC_DEBUG
#define ENABLE_ALLOC_DEBUG 0
#endif

#ifndef ENABLE_SWAP_VERBOSE
#define ENABLE_SWAP_VERBOSE 0
#endif

#define SWAP_LOG_INITIAL 5
#define SWAP_LOG_INTERVAL 1
static int swap_bitmap[MAX_SWAP_PAGES];  // Swap space allocation bitmap (8192 pages = 32MB)
static int total_allocated_pages = 0;
static int used_physical_pages = 0;  // Pages not in free list (actually used)
static int swap_enabled = 0;
static int swap_victim_counter = 0;      // Simple counter for FIFO victim selection
static uintptr_t swap_scan_high = USER_SWAP_PREF_START;  // Preferred (heap) region
static uintptr_t swap_scan_low = USER_LOW_SWAP_START;    // Fallback (code) region
static uint64_t swap_pressure_events = 0;
static uint64_t swap_out_events = 0;
static uint64_t swap_in_events = 0;

static inline int swap_should_log(uint64_t counter) {
#if ENABLE_SWAP_VERBOSE
    return counter <= SWAP_LOG_INITIAL || (counter % SWAP_LOG_INTERVAL) == 0;
#else
    (void)counter;
    return 0;
#endif
}

static inline uintptr_t clamp_va(uintptr_t va, uintptr_t start, uintptr_t end) {
    if (va < start || va >= end) {
        return start;
    }
    return va;
}

static uintptr_t scan_for_victim(uintptr_t *scan_pos, uintptr_t range_start,
                                 uintptr_t range_end, int *pages_scanned) {
    if (range_end <= range_start) {
        return 0;
    }

    uintptr_t test_va = clamp_va(*scan_pos, range_start, range_end);
    uintptr_t start_va = test_va;
    int wrapped = 0;

    while (1) {
        (*pages_scanned)++;
        uintptr_t pte_ptr = get_pteptr_of(test_va, current_running->pgdir);
        if (pte_ptr) {
            PTE *pte = (PTE *)pte_ptr;
            if (*pte & _PAGE_PRESENT) {
                *scan_pos = test_va + PAGE_SIZE;
                if (*scan_pos >= range_end) {
                    *scan_pos = range_start;
                }
                return test_va;
            }
        }

        test_va += PAGE_SIZE;
        if (test_va >= range_end) {
            test_va = range_start;
            wrapped = 1;
        }
        if (wrapped && test_va == start_va) {
            break;
        }
    }

    return 0;
}

static ptr_t alloc_from_free_list(void) {
    if (free_list_head == 0) {
        return 0;
    }

    ptr_t ret = free_list_head;
    free_list_head = *(ptr_t *)free_list_head;
    memset((void *)ret, 0, PAGE_SIZE);
    used_physical_pages++;
    return ret;
}

void init_swap() {
    // Initialize swap bitmap
    for (int i = 0; i < MAX_SWAP_PAGES; i++) {
        swap_bitmap[i] = 0;  // 0 = free, 1 = used
    }

    swap_victim_counter = 0;
    swap_enabled = 1;
    swap_scan_high = USER_SWAP_PREF_START;
    swap_scan_low = USER_LOW_SWAP_START;

    printk("> [SWAP] Swap mechanism initialized:\n");
    printk("> [SWAP]   - Total swap pages: %d (%d MB)\n", MAX_SWAP_PAGES, (MAX_SWAP_PAGES * 4) / 1024);
    printk("> [SWAP]   - Physical memory: %d pages (%d MB)\n", TOTAL_PHYSICAL_PAGES, (TOTAL_PHYSICAL_PAGES * 4) / 1024);
    printk("> [SWAP]   - Swap space range: 0x%x - 0x%x\n",
           SWAP_START_SECTOR, SWAP_START_SECTOR + MAX_SWAP_PAGES * 8);
    printk("> [SWAP]   - Swap buffer: %d pages reserved\n", 10);
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


// Simple swap out - find a swappable user page
int swap_out_page() {
    if (!swap_enabled) return -1;

    swap_out_events++;
    int log_detail = swap_should_log(swap_out_events);
    if (log_detail) {
        printk("> [SWAP] Starting swap out for process %d (event #%lu)\n",
               current_running->pid, swap_out_events);
    }

    // For simplicity, we'll swap out from a fixed range of user virtual addresses
    // This is a very basic approach - scan user address space for a present page

    uintptr_t end_va = 0x7ffffffff000;
    int pages_scanned = 0;

    uintptr_t victim_va = scan_for_victim(&swap_scan_high, USER_SWAP_PREF_START,
                                          end_va, &pages_scanned);
    if (victim_va == 0) {
        uintptr_t low_end = USER_SWAP_PREF_START;
        if (low_end > end_va) {
            low_end = end_va;
        }
        victim_va = scan_for_victim(&swap_scan_low, USER_LOW_SWAP_START,
                                    low_end, &pages_scanned);
    }

    if (victim_va == 0) {
        if (log_detail) {
            printk("> [SWAP] No suitable page found after scanning %d pages (full scan)\n",
                   pages_scanned);
        }
        return -1;
    }

    uintptr_t pte_ptr = get_pteptr_of(victim_va, current_running->pgdir);
    if (!pte_ptr) {
        return -1;
    }

    PTE *pte = (PTE *)pte_ptr;
    uintptr_t pa = get_pa(*pte);
    if (log_detail) {
        printk("> [SWAP] Found swappable page at VA=0x%lx, PA=0x%lx (scanned %d pages)\n",
               victim_va, pa, pages_scanned);
    }

    int swap_idx = alloc_swap_slot();
    if (swap_idx < 0) {
        printk("> [SWAP] No swap space available!\n");
        return -1;
    }

    uintptr_t kva = pa2kva(pa);
    uint32_t sector = SWAP_START_SECTOR + swap_idx * 8;  // 8 sectors per page
    if (log_detail) {
        printk("> [SWAP] Writing page to SD card: swap_idx=%d, sector=0x%x\n",
               swap_idx, sector);
    }
    if (sd_write(kva2pa(kva), 8, sector) != 0) {
        printk("> [SWAP] ERROR: SD write failed for swap_idx=%d (sector=0x%x)\n",
               swap_idx, sector);
        free_swap_slot(swap_idx);
        return -1;
    }

    *pte = (*pte & ~_PAGE_PRESENT) | ((uint64_t)swap_idx << _PAGE_PFN_SHIFT) | _PAGE_SOFT;
    local_flush_tlb_page(victim_va);
    freePage(kva);

    // Update process page count - page is swapped out
    current_running->allocated_pages--;

    if (log_detail) {
        printk("> [SWAP] Page swapped out successfully: VA=0x%lx -> swap_idx=%d, process pages: %d\n",
               victim_va, swap_idx, current_running->allocated_pages);
    }
    return swap_idx;
}

// Swap in a page from SD card
void swap_in_page(uintptr_t va, uintptr_t pgdir, int swap_idx) {
    if (!swap_enabled || swap_idx < 0 || swap_idx >= MAX_SWAP_PAGES) return;

    swap_in_events++;
    int log_detail = swap_should_log(swap_in_events);
    if (log_detail) {
        printk("> [SWAP] Swapping in page: VA=0x%lx, swap_idx=%d (event #%lu)\n",
               va, swap_idx, swap_in_events);
    }

    // Allocate a new physical page
    ptr_t new_page = allocPage(1);
    if (new_page == 0) {
        // Out of memory, need to swap out first
        printk("> [SWAP] No memory for swap in, triggering swap out first\n");
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
    if (log_detail) {
        printk("> [SWAP] Reading page from SD card: swap_idx=%d, sector=0x%x\n",
               swap_idx, sector);
    }
    if (sd_read(kva2pa(new_page), 8, sector) != 0) {
        printk("> [SWAP] ERROR: SD read failed for swap_idx=%d (sector=0x%x)\n",
               swap_idx, sector);
        freePage(new_page);
        return;
    }

    // Update PTE
    uintptr_t pte_ptr = get_pteptr_of(va, pgdir);
    if (pte_ptr) {
        PTE *pte = (PTE *)pte_ptr;
        set_pfn(pte, kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE |
                          _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
        local_flush_tlb_page(va);

        // Find which process this page belongs to and update its count
        // For now, we update current_running's count (assuming it's for current process)
        pcb_t *target_process = current_running;
        // In a more complete implementation, we would search for the process that owns pgdir
        target_process->allocated_pages++;

        if (log_detail) {
            printk("> [SWAP] Page swapped in successfully: swap_idx=%d -> VA=0x%lx, PA=0x%lx, process %d pages: %d\n",
                   swap_idx, va, kva2pa(new_page), target_process->pid, target_process->allocated_pages);
        }
    } else {
        printk("> [SWAP] ERROR: Could not find PTE for VA=0x%lx\n", va);
    }

    // Free swap slot
    free_swap_slot(swap_idx);
    if (log_detail) {
        printk("> [SWAP] Released swap slot %d\n", swap_idx);
    }
}

ptr_t allocPage(int numPage)
{
    // Debug: Always print allocation info
#if ENABLE_ALLOC_DEBUG
    static int alloc_count = 0;
    alloc_count++;
    if (alloc_count > 57340) {
        printk("[ALLOC] alloc_count=%d, requested=%d, allocated=%d/%d (%d KB), kernMemCurr=0x%lx\n",
               alloc_count, numPage, total_allocated_pages, TOTAL_PHYSICAL_PAGES,
               total_allocated_pages * (PAGE_SIZE / 1024), kernMemCurr);
    }
#endif

    if (numPage == 1) {
        ptr_t ret = alloc_from_free_list();
        if (ret != 0) {
            return ret;
        }
    }

    // Always go to physical memory allocation to test swap
    // printk("[ALLOC] free_list_head=0x%lx, forcing physical memory allocation\n", free_list_head);

    // Check if we have enough physical pages based on actual usage
    if (used_physical_pages + numPage > TOTAL_PHYSICAL_PAGES) {
        if (swap_enabled) {
            swap_pressure_events++;
            int log_pressure = swap_should_log(swap_pressure_events);
            if (log_pressure) {
                printk("> [SWAP] Memory pressure detected! Need %d pages, have %d/%d allocated (event #%lu)\n",
                       numPage, used_physical_pages, TOTAL_PHYSICAL_PAGES, swap_pressure_events);
                printk("> [SWAP] CURRENT STATUS: pages=%d, need=%d, total=%d\n",
                       used_physical_pages, numPage, TOTAL_PHYSICAL_PAGES);
            }

            // Try to swap out pages
            int swap_attempts = 0;
            // Use used_physical_pages instead of total_allocated_pages for swap decision
            // Keep 10 pages free for kernel operations
            if (log_pressure) {
                printk("> [SWAP] Checking swap condition: used_pages=%d + numPage=%d > %d-10 ?\n",
                       used_physical_pages, numPage, TOTAL_PHYSICAL_PAGES);
            }
            while (used_physical_pages + numPage > TOTAL_PHYSICAL_PAGES - 10) {
                swap_attempts++;
                int swapped_idx = swap_out_page();
                if (swapped_idx < 0) {
                    printk("> [SWAP] Failed to swap out page after %d attempts\n", swap_attempts);
                    break;  // Can't swap more
                }
                if (log_pressure) {
                    printk("> [SWAP] Successfully swapped out page (attempt %d), memory: total=%d/%d, used=%d\n",
                           swap_attempts, total_allocated_pages, TOTAL_PHYSICAL_PAGES, used_physical_pages);
                }

                // After swap_out, try to use freed page from free list
                if (numPage == 1) {
                    ptr_t ret = alloc_from_free_list();
                    if (ret != 0) {
                        if (log_pressure) {
                            printk("> [SWAP] Using freed page from swap, total swap attempts: %d\n", swap_attempts);
                        }
                        return ret;
                    }
                }
            }
        }

        // Still not enough memory
        if (used_physical_pages + numPage > TOTAL_PHYSICAL_PAGES) {
            return 0;  // Allocation failed
        }
    }

    // Allocate from kernel memory (only when free list is empty)
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;

    // IMPORTANT: Check for memory overflow!
    ptr_t kernMemEnd = 0xffffffc060000000ULL;  // 1.5GB virtual address limit (0x60000000)
    if (kernMemCurr > kernMemEnd) {
        printk("[ALLOC] ERROR: kernMemCurr overflow! curr=0x%lx, limit=0x%lx\n", kernMemCurr, kernMemEnd);
        printk("[ALLOC] Allocated pages: %d/%d, kernMemCurr overflowed!\n", total_allocated_pages, TOTAL_PHYSICAL_PAGES);
    }

    // Track allocation
    total_allocated_pages += numPage;

    // Track used pages (not in free list)
    // Since we're allocating from kernMemCurr (not from free list),
    // these pages are now "used"
    used_physical_pages += numPage;

    return ret;
}

void freePage(ptr_t baseAddr) {
    // 将释放的页插入空闲链表头部
    *(ptr_t *)baseAddr = free_list_head;
    free_list_head = baseAddr;

    // 注意：不减total_allocated_pages
    // 因为页面仍然被分配（只是在空闲链表中）
    // total_allocated_pages代表已分配的总页面数，包括空闲和使用的

    // 但是要减少used_physical_pages，因为页面现在在空闲链表中
    used_physical_pages--;

    // Debug: print counter values
    // if (total_allocated_pages % 1000 == 0) {
    //     printk("[FREE] freed page at 0x%lx: total=%d, used=%d\n",
    //            baseAddr, total_allocated_pages, used_physical_pages);
    // }
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

    // Add unallocated memory based on current usage
    size_t unallocated_pages = 0;
    if (TOTAL_PHYSICAL_PAGES > free_pages_count + used_physical_pages) {
        unallocated_pages = TOTAL_PHYSICAL_PAGES - free_pages_count - used_physical_pages;
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
        if (new_page == 0) {
            // Try to swap if we hit the process limit
            if (current_running->allocated_pages >= MAX_PAGES_PER_PROCESS) {
                printk("[MM] Process %d hit memory limit (%d >= %d), trying swap\n",
                       current_running->pid, current_running->allocated_pages, MAX_PAGES_PER_PROCESS);
                // Swap out one of this process's pages
                if (swap_out_page() >= 0) {
                    new_page = allocPage(1);
                }
            }

            if (new_page == 0) {
                printk("[MM] alloc_page_helper: failed to allocate L2 page table\n");
                return 0;
            }
        }
        clear_pgdir(new_page);
        // [修正] 中间页表项：只给 V 位，绝对不要给 U 位
        set_pfn(&pgdir_kva[vpn2], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgdir_kva[vpn2], _PAGE_PRESENT);

        // Update process page count
        current_running->allocated_pages++;
    }

    // 1. 获取 L1 页表的物理地址 PA，并转为 KVA
    PTE *pmd = (PTE *)pa2kva(get_pa(pgdir_kva[vpn2]));
    // 2. L1 索引：取 va 的 [29:21] 位
    uint64_t vpn1 = (va >> 21) & 0x1ff;
    if ((pmd[vpn1] & _PAGE_PRESENT) == 0) {
        ptr_t new_page = allocPage(1);
        if (new_page == 0) {
            // Try to swap if we hit the process limit
            if (current_running->allocated_pages >= MAX_PAGES_PER_PROCESS) {
                printk("[MM] Process %d hit memory limit (%d >= %d), trying swap\n",
                       current_running->pid, current_running->allocated_pages, MAX_PAGES_PER_PROCESS);
                // Swap out one of this process's pages
                if (swap_out_page() >= 0) {
                    new_page = allocPage(1);
                }
            }

            if (new_page == 0) {
                printk("[MM] alloc_page_helper: failed to allocate L1 page table\n");
                return 0;
            }
        }
        clear_pgdir(new_page);
        // [修正] 中间页表项：只给 V 位，绝对不要给 U 位
        set_pfn(&pmd[vpn1], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);

        // Update process page count
        current_running->allocated_pages++;
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
    if (final_page == 0) {
        // Try to swap if we hit the process limit
        if (current_running->allocated_pages >= MAX_PAGES_PER_PROCESS) {
            printk("[MM] Process %d hit memory limit (%d >= %d), trying swap\n",
                   current_running->pid, current_running->allocated_pages, MAX_PAGES_PER_PROCESS);
            // Swap out one of this process's pages
            if (swap_out_page() >= 0) {
                final_page = allocPage(1);
            }
        }

        if (final_page == 0) {
            printk("[MM] alloc_page_helper failed: out of memory!\n");
            return 0;  // Return 0 to indicate failure
        }
    }
    // 清零新分配的页，防止访问到脏数据
    memset((void *)final_page, 0, PAGE_SIZE);
    set_pfn(&pte[vpn0], kva2pa(final_page) >> NORMAL_PAGE_SHIFT);
    // [保持] 叶子节点：必须给足 V, R, W, X, U, A, D
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);

    // Update process page count
    current_running->allocated_pages++;

    return final_page;
}

uintptr_t shm_page_get(int key) { return 0; }
void shm_page_dt(uintptr_t addr) {}

// Pipe management for Task 5
static pipe_t pipes[MAX_PIPES];
static int pipe_initialized = 0;

// Free list for pipe_page_t metadata entries (packed per page)
static pipe_page_t *free_pipe_pages = NULL;

static pipe_page_t *alloc_pipe_page_struct(void) {
    if (!free_pipe_pages) {
        ptr_t chunk = allocPage(1);
        if (!chunk) {
            printk("> [PIPE] Failed to allocate metadata page for pipe\n");
            return NULL;
        }
        size_t entries = PAGE_SIZE / sizeof(pipe_page_t);
        pipe_page_t *array = (pipe_page_t *)chunk;
        for (size_t i = 0; i < entries; i++) {
            array[i].next = free_pipe_pages;
            free_pipe_pages = &array[i];
        }
    }

    pipe_page_t *entry = free_pipe_pages;
    free_pipe_pages = entry->next;
    entry->next = NULL;
    return entry;
}

static void release_pipe_page_struct(pipe_page_t *page) {
    page->next = free_pipe_pages;
    free_pipe_pages = page;
}

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
        init_list_head(&pipes[i].reader_queue);
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
            if (page->pa) {
                freePage(pa2kva(page->pa));
            }
            release_pipe_page_struct(page);
            page = next;
        }

        // Wake up any blocked readers so they can exit
        free_block_list(&pipes[pipe_idx].reader_queue);
        init_list_head(&pipes[pipe_idx].reader_queue);

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
    init_list_head(&pipes[idx].reader_queue);

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
        pipe_page_t *page = alloc_pipe_page_struct();
        if (!page) {
            return -1;  // Out of memory for metadata
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

        // Unmap the page from the sender so it won't be freed twice or modified
        *pte = 0;
        local_flush_tlb_page(page_va);

        // Add to pipe
        if (pipes[pipe_idx].tail) {
            pipes[pipe_idx].tail->next = page;
        } else {
            pipes[pipe_idx].head = page;
        }
        pipes[pipe_idx].tail = page;
        pipes[pipe_idx].total_pages++;
    }

    if (pipes[pipe_idx].reader_queue.next != &pipes[pipe_idx].reader_queue) {
        free_block_list(&pipes[pipe_idx].reader_queue);
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

    uintptr_t dst_va = (uintptr_t)dst;
    size_t total_copied = 0;

    // Take pages from pipe and remap to receiver
    while (total_copied < length) {
        while (!pipes[pipe_idx].head) {
            if (!pipes[pipe_idx].is_open) {
                return total_copied;  // Pipe closed, nothing more to read
            }
            do_block(&current_running->list, &pipes[pipe_idx].reader_queue);
            do_scheduler();
            if (!pipes[pipe_idx].is_open && !pipes[pipe_idx].head) {
                return total_copied;
            }
        }

        pipe_page_t *page = pipes[pipe_idx].head;
        pipes[pipe_idx].head = page->next;
        if (!pipes[pipe_idx].head) {
            pipes[pipe_idx].tail = NULL;
        }
        pipes[pipe_idx].total_pages--;

        // Get the PTE for destination address
        uintptr_t dst_page_va = dst_va + total_copied;
        uintptr_t pte_ptr = get_pteptr_of(dst_page_va, current_running->pgdir);
        ptr_t temp_page = 0;
        if (!pte_ptr) {
            temp_page = alloc_page_helper(dst_page_va, current_running->pgdir);
            if (temp_page == 0) {
                release_pipe_page_struct(page);
                return total_copied;
            }
            pte_ptr = get_pteptr_of(dst_page_va, current_running->pgdir);
        }

        if (pte_ptr) {
            PTE *pte = (PTE *)pte_ptr;

            // Remap the physical page to destination
            set_pfn(pte, page->pa >> NORMAL_PAGE_SHIFT);
            set_attribute(pte, _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);

            // Flush TLB for this page
            local_flush_tlb_page(dst_page_va);

            total_copied += page->size;
        }

        if (temp_page) {
            freePage(temp_page);
        }

        // Return page structure to free list
        release_pipe_page_struct(page);
    }

    return total_copied;
}
