/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                                   Memory Management
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */
#ifndef MM_H
#define MM_H

#include <pgtable.h>
#include <type.h>

#define MAP_KERNEL 1
#define MAP_USER 2
#define MEM_SIZE 32
#define PAGE_SIZE 4096 // 4K
#define INIT_KERNEL_STACK 0xffffffc052000000
#define INIT_USER_STACK 0xffffffc052500000
#define FREEMEM_KERNEL (INIT_KERNEL_STACK + 2 * PAGE_SIZE)
#define FREEMEM_USER INIT_USER_STACK

/* Rounding; only works for n = power of two */
#define ROUND(a, n) (((((uint64_t)(a)) + (n) - 1)) & ~((n) - 1))
#define ROUNDDOWN(a, n) (((uint64_t)(a)) & ~((n) - 1))

// Swap space configuration
#define SWAP_START_SECTOR 0x200000  // Start sector for swap space on SD card
#define MAX_SWAP_PAGES 1024         // Maximum pages that can be swapped
#define TOTAL_PHYSICAL_PAGES 512     // Total physical pages (2MB / 4KB) - reduced for swap testing

extern ptr_t allocPage(int numPage);

void freePage(ptr_t baseAddr);

#define USER_STACK_ADDR 0xf00010000

extern void *kmalloc(size_t size);
extern void share_pgtable(uintptr_t dest_pgdir, uintptr_t src_pgdir);
extern uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir);
extern void free_pgtable_pages(uintptr_t pgdir);

// Swap mechanism functions
extern void init_swap();
extern int swap_out_page();
extern void swap_in_page(uintptr_t va, uintptr_t pgdir, int swap_idx);

// Memory statistics
extern size_t get_free_memory();

// TODO [P4-task4]:shm_page_get/dt */
uintptr_t shm_page_get(int key);
void shm_page_dt(uintptr_t addr);

// Memory page pipe structure for Task 5
#define MAX_PIPES 32

typedef struct pipe_page {
    void *kva;                   // Kernel virtual address of the page
    uintptr_t pa;                // Physical address of the page
    int in_use;                  // Whether this page contains data
    int size;                    // Size of data in the page
    struct pipe_page *next;      // Next page in the pipe
} pipe_page_t;

typedef struct pipe {
    char name[32];               // Pipe name
    int ref_count;               // Number of processes using this pipe
    pipe_page_t *head;           // Head of page list (oldest)
    pipe_page_t *tail;           // Tail of page list (newest)
    int total_pages;             // Total pages in pipe
    int is_open;                 // Whether pipe is open
} pipe_t;

// Pipe management functions
extern int do_pipe_open(const char *name);
extern long do_pipe_give_pages(int pipe_idx, void *src, size_t length);
extern long do_pipe_take_pages(int pipe_idx, void *dst, size_t length);
extern void do_pipe_close(int pipe_idx);

#endif /* MM_H */
