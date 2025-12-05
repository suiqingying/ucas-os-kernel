#include <os/mm.h>
#include <os/string.h>
#include <printk.h>
#include <type.h>
// 使用内核虚拟地址管理动态内存
static ptr_t kernMemCurr = FREEMEM_KERNEL;
static ptr_t free_list_head = 0; // 空闲链表头

// [Debug] 声明一个变量记录上一次的值，用于检测回滚
static ptr_t last_mem_curr = 0;

ptr_t allocPage(int numPage)
{
    // 必须确保 kernMemCurr 是静态/全局的，并且每次都更新
    ptr_t ret = ROUND(kernMemCurr, PAGE_SIZE);
    kernMemCurr = ret + numPage * PAGE_SIZE;
    return ret;
}

void freePage(ptr_t baseAddr) {
    // 将释放的页插入空闲链表头部
    *(ptr_t *)baseAddr = free_list_head;
    free_list_head = baseAddr;
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
    set_pfn(&pte[vpn0], kva2pa(final_page) >> NORMAL_PAGE_SHIFT);
    // [保持] 叶子节点：必须给足 V, R, W, X, U, A, D
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);

    return final_page;
}

uintptr_t shm_page_get(int key) { return 0; }
void shm_page_dt(uintptr_t addr) {}