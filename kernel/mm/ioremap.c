#include <os/ioremap.h>
#include <os/mm.h>
#include <pgtable.h>
#include <type.h>

// maybe you can map it to IO_ADDR_START ?
static uintptr_t io_base = IO_ADDR_START;

static void map_io_page(uintptr_t va, uintptr_t pa)
{
    PTE *pgdir = (PTE *)pa2kva(PGDIR_PA);

    va &= VA_MASK;
    uint64_t vpn2 = (va >> 30) & 0x1ff;
    uint64_t vpn1 = (va >> 21) & 0x1ff;
    uint64_t vpn0 = (va >> 12) & 0x1ff;

    if ((pgdir[vpn2] & _PAGE_PRESENT) == 0) {
        ptr_t new_page = allocPage(1);
        clear_pgdir(new_page);
        set_pfn(&pgdir[vpn2], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pgdir[vpn2], _PAGE_PRESENT);
    }

    PTE *pmd = (PTE *)pa2kva(get_pa(pgdir[vpn2]));
    if ((pmd[vpn1] & _PAGE_PRESENT) == 0) {
        ptr_t new_page = allocPage(1);
        clear_pgdir(new_page);
        set_pfn(&pmd[vpn1], kva2pa(new_page) >> NORMAL_PAGE_SHIFT);
        set_attribute(&pmd[vpn1], _PAGE_PRESENT);
    }

    PTE *pte = (PTE *)pa2kva(get_pa(pmd[vpn1]));
    set_pfn(&pte[vpn0], pa >> NORMAL_PAGE_SHIFT);
    set_attribute(&pte[vpn0],
                  _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | _PAGE_ACCESSED |
                      _PAGE_DIRTY);
}


void *ioremap(unsigned long phys_addr, unsigned long size) {
    // map one specific physical region to virtual address
    if (size == 0) {
        return NULL;
    }
    uintptr_t pa_start = ROUNDDOWN(phys_addr, PAGE_SIZE);
    uintptr_t offset = phys_addr - pa_start;
    uintptr_t bytes = ROUND(size + offset, PAGE_SIZE);

    uintptr_t va_start = io_base;
    io_base += bytes;

    for (uintptr_t i = 0; i < bytes; i += PAGE_SIZE) {
        map_io_page(va_start + i, pa_start + i);
        local_flush_tlb_page(va_start + i);
    }

    return (void *)(va_start + offset);
}

void iounmap(void *io_addr) {
    // a very naive iounmap() is OK
    // maybe no one would call this function?
    (void)io_addr;
}
