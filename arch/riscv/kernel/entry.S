#include <asm.h>
#include <csr.h>
#include <asm/regs.h>

    .macro                           SAVE_CONTEXT
    /* save all general purpose registers here! */
    /* HINT: Pay attention to the function of tp and sp, and save them carefully! */

    mv                               t0, sp // use t0 as temporary register to store sp
    sd                               sp, PCB_USER_SP(tp) // store user stack
    ld                               sp, PCB_KERNEL_SP(tp) // recover kernel stack

    addi                             sp, sp, -OFFSET_SIZE

    sd                               x0, OFFSET_REG_ZERO(sp)
    sd                               x1, OFFSET_REG_RA(sp)
    sd                               t0, OFFSET_REG_SP(sp)
    sd                               x3, OFFSET_REG_GP(sp)
    sd                               x4, OFFSET_REG_TP(sp)
    sd                               x5, OFFSET_REG_T0(sp)
    sd                               x6, OFFSET_REG_T1(sp)
    sd                               x7, OFFSET_REG_T2(sp)
    sd                               x8, OFFSET_REG_S0(sp)
    sd                               x9, OFFSET_REG_S1(sp)
    sd                               x10, OFFSET_REG_A0(sp)
    sd                               x11, OFFSET_REG_A1(sp)
    sd                               x12, OFFSET_REG_A2(sp)
    sd                               x13, OFFSET_REG_A3(sp)
    sd                               x14, OFFSET_REG_A4(sp)
    sd                               x15, OFFSET_REG_A5(sp)
    sd                               x16, OFFSET_REG_A6(sp)
    sd                               x17, OFFSET_REG_A7(sp)
    sd                               x18, OFFSET_REG_S2(sp)
    sd                               x19, OFFSET_REG_S3(sp)
    sd                               x20, OFFSET_REG_S4(sp)
    sd                               x21, OFFSET_REG_S5(sp)
    sd                               x22, OFFSET_REG_S6(sp)
    sd                               x23, OFFSET_REG_S7(sp)
    sd                               x24, OFFSET_REG_S8(sp)
    sd                               x25, OFFSET_REG_S9(sp)
    sd                               x26, OFFSET_REG_S10(sp)
    sd                               x27, OFFSET_REG_S11(sp)
    sd                               x28, OFFSET_REG_T3(sp)
    sd                               x29, OFFSET_REG_T4(sp)
    sd                               x30, OFFSET_REG_T5(sp)
    sd                               x31, OFFSET_REG_T6(sp)

    /*
    *                                Disable user-mode memory access as it should only be set in the
    *                                actual user copy routines.
    *
    *                                Disable the FPU to detect illegal usage of floating point in kernel
    *                                space.
    */
    li                               t0, SR_SUM | SR_FS

    /* save sstatus, sepc, stval and scause on kernel stack */
    csrr                             t0, sstatus
    sd                               t0, OFFSET_REG_SSTATUS(sp)
    csrr                             t0, sepc
    sd                               t0, OFFSET_REG_SEPC(sp)
    csrr                             t0, stval
    sd                               t0, OFFSET_REG_SBADADDR(sp)
    csrr                             t0, scause
    sd                               t0, OFFSET_REG_SCAUSE(sp)

    .endm

    .macro                           RESTORE_CONTEXT
/* Restore                          all general purpose registers and sepc, sstatus */
/* HINT:
    Pay                              attention to sp again! */
    ld                               x0, OFFSET_REG_ZERO(sp)
    ld                               ra, OFFSET_REG_RA(sp)
    ld                               gp, OFFSET_REG_GP(sp)
    ld                               tp, OFFSET_REG_TP(sp)
    ld                               t0, OFFSET_REG_T0(sp)
    ld                               t1, OFFSET_REG_T1(sp)
    ld                               t2, OFFSET_REG_T2(sp)
    ld                               s0, OFFSET_REG_S0(sp)
    ld                               s1, OFFSET_REG_S1(sp)
    ld                               a0, OFFSET_REG_A0(sp)
    ld                               a1, OFFSET_REG_A1(sp)
    ld                               a2, OFFSET_REG_A2(sp)
    ld                               a3, OFFSET_REG_A3(sp)
    ld                               a4, OFFSET_REG_A4(sp)
    ld                               a5, OFFSET_REG_A5(sp)
    ld                               a6, OFFSET_REG_A6(sp)
    ld                               a7, OFFSET_REG_A7(sp)
    ld                               s2, OFFSET_REG_S2(sp)
    ld                               s3, OFFSET_REG_S3(sp)
    ld                               s4, OFFSET_REG_S4(sp)
    ld                               s5, OFFSET_REG_S5(sp)
    ld                               s6, OFFSET_REG_S6(sp)
    ld                               s7, OFFSET_REG_S7(sp)
    ld                               s8, OFFSET_REG_S8(sp)
    ld                               s9, OFFSET_REG_S9(sp)
    ld                               s10, OFFSET_REG_S10(sp)
    ld                               s11, OFFSET_REG_S11(sp)
    ld                               t3, OFFSET_REG_SSTATUS(sp)
    ld                               t4, OFFSET_REG_SEPC(sp)

    csrw                             sstatus, t3
    csrw                             sepc, t4

    ld                               t3, OFFSET_REG_T3(sp)
    ld                               t4, OFFSET_REG_T4(sp)
    ld                               t5, OFFSET_REG_T5(sp)
    ld                               t6, OFFSET_REG_T6(sp)

    addi                             sp, sp, OFFSET_SIZE
    sd                               sp, PCB_KERNEL_SP(tp)
    ld                               sp, PCB_USER_SP(tp)
    .endm

    ENTRY(enable_preempt)
    not                              t0, x0
    csrs                             CSR_SIE, t0
    jr                               ra
    ENDPROC(enable_preempt)

    ENTRY(disable_preempt)
    csrw                             CSR_SIE, zero
    jr                               ra
    ENDPROC(disable_preempt)

    ENTRY(enable_interrupt)
    li                               t0, SR_SIE
    csrs                             CSR_SSTATUS, t0
    jr                               ra
    ENDPROC(enable_interrupt)

    ENTRY(disable_interrupt)
    li                               t0, SR_SIE
    csrc                             CSR_SSTATUS, t0
    jr                               ra
    ENDPROC(disable_interrupt)

// NOTE: the address of previous pcb in a0
// NOTE: the address of next pcb in a1
    ENTRY(switch_to)
    addi                             sp, sp, -(SWITCH_TO_SIZE)

/* save all callee save registers on kernel stack, see the definition of `struct switchto_context` in sched.h*/

    sd                               sp, PCB_KERNEL_SP(a0)

    // save switch_to_context regs
    sd                               ra, SWITCH_TO_RA(sp)
    sd                               sp, SWITCH_TO_SP(sp)
    sd                               s0, SWITCH_TO_S0(sp)
    sd                               s1, SWITCH_TO_S1(sp)
    sd                               s2, SWITCH_TO_S2(sp)
    sd                               s3, SWITCH_TO_S3(sp)
    sd                               s4, SWITCH_TO_S4(sp)
    sd                               s5, SWITCH_TO_S5(sp)
    sd                               s6, SWITCH_TO_S6(sp)
    sd                               s7, SWITCH_TO_S7(sp)
    sd                               s8, SWITCH_TO_S8(sp)
    sd                               s9, SWITCH_TO_S9(sp)
    sd                               s10, SWITCH_TO_S10(sp)
    sd                               s11, SWITCH_TO_S11(sp)

/* restore all callee save registers from kernel stack,
 *  see the definition of `struct switchto_context` in sched.h
 */

# switch current running

    addi                             tp, a1, 0
    ld                               sp, PCB_KERNEL_SP(a1)                                               # sp is the kernel sp of next task

# restore switch_to_context regs
    ld                               ra, SWITCH_TO_RA(sp)
    ld                               sp, SWITCH_TO_SP(sp)                                                # restore stack
    ld                               s0, SWITCH_TO_S0(sp)
    ld                               s1, SWITCH_TO_S1(sp)
    ld                               s2, SWITCH_TO_S2(sp)
    ld                               s3, SWITCH_TO_S3(sp)
    ld                               s4, SWITCH_TO_S4(sp)
    ld                               s5, SWITCH_TO_S5(sp)
    ld                               s6, SWITCH_TO_S6(sp)
    ld                               s7, SWITCH_TO_S7(sp)
    ld                               s8, SWITCH_TO_S8(sp)
    ld                               s9, SWITCH_TO_S9(sp)
    ld                               s10, SWITCH_TO_S10(sp)
    ld                               s11, SWITCH_TO_S11(sp)

    addi                             sp, sp, SWITCH_TO_SIZE
    sd                               sp, PCB_KERNEL_SP(a1)                                               # revise the kernel sp, write back to pcb
    jr                               ra
    ENDPROC(switch_to)

    ENTRY(ret_from_exception)
    /*                               restore context via provided macro and return to sepc */
/* HINT:
    remember                         to check your sp, does it point to the right address? */
    call unlock_kernel
    RESTORE_CONTEXT
    sret
    ENDPROC(ret_from_exception)

ENTRY(exception_handler_entry)

    /* save context via the provided macro */
    SAVE_CONTEXT

    /* --------------------------------------------------------- */
    /* Acquire Big Kernel Lock                        */
    /* --------------------------------------------------------- */
    call lock_kernel

    /* --------------------------------------------------------- */
    /* Call interrupt_helper                                     */
    /* NOTE: We must prepare arguments AFTER lock_kernel,        */
    /* because lock_kernel function call might modify a0/a1/a2.  */
    /* --------------------------------------------------------- */
    addi                             a0, sp, 0      /* arg0: regs pointer */
    csrr                             a1, stval      /* arg1: stval */
    csrr                             a2, scause     /* arg2: scause */
    call                             interrupt_helper

    /* --------------------------------------------------------- */
    /* Release Big Kernel Lock                        */
    /* --------------------------------------------------------- */
    # call unlock_kernel

    /* Return to ret_from_exception to restore context */
    j                                ret_from_exception
    
    ENDPROC(exception_handler_entry)