/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#include <asm.h>
#include <csr.h>

.equ P_KERNEL_STACK, 0xffffffc050500000 // Primary core's stack
.equ S_KERNEL_STACK, 0xffffffc050600000 // Secondary core's stack

.section ".entry_function","ax"
ENTRY(_start)
  /* Mask all interrupts */
  csrw CSR_SIE, zero
  csrw CSR_SIP, zero

  li t0, SR_SUM
  csrs CSR_SSTATUS, t0

  /* Read hart id. This will be the first argument to main(hartid). */
  csrr a0, CSR_MHARTID
  
  /* Check if it's the primary core */
  bnez a0, secondary_core_setup

/************************************************************/
/*               Primary Core (Hart 0) Setup                */
/************************************************************/
primary_core_setup:
  /* clear BSS for flat non-ELF images */
  la t0, __bss_start
  la t1, __BSS_END__  
  clear_bss:
    sw zero, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, clear_bss

  /* setup C environment */
  bss_done:
    /* Setup tp to point to the initial PCB for the primary core */
    la tp, pid0_pcb
    /* Setup stack pointer for the primary core */
    li sp, P_KERNEL_STACK
    
    /* Jump to main(hartid). The hartid (0) is already in a0. */
    j main

/************************************************************/
/*              Secondary Core (Hart > 0) Setup             */
/************************************************************/
secondary_core_setup:
    /* Setup tp to point to the initial PCB for the secondary core */
    la tp, s_pid0_pcb 
    /* Setup stack pointer for the secondary core */
    li sp, S_KERNEL_STACK
    
    /* Jump to main(hartid). The hartid (>0) is already in a0. */
    j main

loop:
  wfi
  j loop

END(_start)
