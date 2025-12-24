#include <assert.h>
#include <os/irq.h>
#include <os/fs.h>
#include <os/kernel.h>
#include <os/mm.h>
#include <os/net.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/time.h>
#include <plic.h>
#include <printk.h>
#include <screen.h>
#define SCAUSE_IRQ_MASK 0x8000000000000000

handler_t irq_table[IRQC_COUNT];
handler_t exc_table[EXCC_COUNT];

void interrupt_helper(regs_context_t *regs, uint64_t stval, uint64_t scause) {
    // interrupt handler.
    // call corresponding handler by the value of `scause`
    if (scause & SCAUSE_IRQ_MASK) // 中断
        irq_table[scause & ~SCAUSE_IRQ_MASK](regs, stval, scause);
    else {
        exc_table[scause](regs, stval, scause);
    }
}

void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause) {
    // clock interrupt handler.
    // Note: use bios_set_timer to reset the timer and remember to reschedule
    bios_set_timer(get_ticks() + TIMER_INTERVAL); // 下一次查询中断的时间

    fs_cache_tick();
    net_stream_timer();
    net_unblock_stream_recv();
    do_scheduler();
}

void handle_irq_ext(regs_context_t *regs, uint64_t stval, uint64_t scause) {
    // external interrupt handler.
    // Note: plic_claim and plic_complete will be helpful ...
    int hwirq = (int)plic_claim();
    if (hwirq == 0) return;
    if (hwirq == PLIC_E1000_QEMU_IRQ || hwirq == PLIC_E1000_PYNQ_IRQ) {
        net_handle_irq();
    }
    plic_complete(hwirq);
}

void init_exception() {
    /* initialize exc_table */
    /* NOTE: handle_syscall, handle_other, etc.*/
    exc_table[EXCC_INST_MISALIGNED] = handle_other;
    exc_table[EXCC_INST_ACCESS] = handle_other;
    exc_table[EXCC_BREAKPOINT] = handle_other;
    exc_table[EXCC_LOAD_ACCESS] = handle_other;
    exc_table[EXCC_STORE_ACCESS] = handle_other;
    exc_table[EXCC_SYSCALL] = handle_syscall;
    exc_table[EXCC_INST_PAGE_FAULT] = handle_page_fault;
    exc_table[EXCC_LOAD_PAGE_FAULT] = handle_page_fault;
    exc_table[EXCC_STORE_PAGE_FAULT] = handle_page_fault;
    /* initialize irq_table */
    /* NOTE: handle_int, handle_other, etc.*/
    irq_table[IRQC_U_SOFT] = handle_other;
    irq_table[IRQC_S_SOFT] = handle_other;
    irq_table[IRQC_M_SOFT] = handle_other;
    irq_table[IRQC_U_TIMER] = handle_other;
    irq_table[IRQC_S_TIMER] = handle_irq_timer;
    irq_table[IRQC_M_TIMER] = handle_other;
    irq_table[IRQC_U_EXT] = handle_other;
    irq_table[IRQC_S_EXT] = handle_irq_ext;
    irq_table[IRQC_M_EXT] = handle_other;

    /* set up the entrypoint of exceptions */
    setup_exception();
}

void handle_other(regs_context_t *regs, uint64_t stval, uint64_t scause) {
    char *reg_name[] = {
        "zero ", " ra  ", " sp  ", " gp  ", " tp  ",
        " t0  ", " t1  ", " t2  ", "s0/fp", " s1  ",
        " a0  ", " a1  ", " a2  ", " a3  ", " a4  ",
        " a5  ", " a6  ", " a7  ", " s2  ", " s3  ",
        " s4  ", " s5  ", " s6  ", " s7  ", " s8  ",
        " s9  ", " s10 ", " s11 ", " t3  ", " t4  ",
        " t5  ", " t6  "};
    for (int i = 0; i < 32; i += 3) {
        for (int j = 0; j < 3 && i + j < 32; ++j) {
            printk("%s : %016lx ", reg_name[i + j], regs->regs[i + j]);
        }
        printk("\n\r");
    }
    printk("sstatus: 0x%lx sbadaddr: 0x%lx scause: %lu\n\r",
           regs->sstatus, regs->sbadaddr, regs->scause);
    printk("sepc: 0x%lx\n\r", regs->sepc);
    printk("tval: 0x%lx cause: 0x%lx\n", stval, scause);
    assert(0);
}

void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause) {
    // 对齐到页边界
    uintptr_t fault_va = stval & ~(PAGE_SIZE - 1);
    
    // Check if page is swapped out
    uintptr_t pte_ptr = get_pteptr_of(fault_va, current_running->pgdir);
    if (pte_ptr) {
        PTE *pte = (PTE *)pte_ptr;
        // Check if page is in swap (SOFT bit set, PRESENT bit clear)
        if ((*pte & _PAGE_SOFT) && !(*pte & _PAGE_PRESENT)) {
            // Extract swap index from PFN field (no truncation)
            int swap_idx = (int)get_pfn(*pte);
            swap_in_page(fault_va, current_running->pgdir, swap_idx);
            local_flush_tlb_page(fault_va);
            return;
        }
    }

    // 建立映射（alloc_page_helper会检查是否已存在）
    alloc_page_helper(fault_va, current_running->pgdir);
    
    // 刷新TLB（只刷新这一页即可）
    local_flush_tlb_page(fault_va);
}
