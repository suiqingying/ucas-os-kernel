#include <asm.h>
// # define USER_STACK      0x52500000
.section ".entry_function","ax"
ENTRY(_start)
    // 针对prj2,其实压栈过程可以省略,但是为了代码的roubustness,保留
    /* setup C runtime environment for the user program */
    addi t2, sp, 0
    # la sp, USER_STACK // wrong core reason
    addi sp, sp, -16         # 分配空间
    sd t2, 0(sp)           # 压栈保存原sp
    sd ra, 8(sp)           # 压栈保存ra

    la t0, __bss_start
    la t1, __BSS_END__
clear_bss:
    sw zero, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, clear_bss
    /* enter main function */
    jal main                        # 跳转到用户程序main函数

    /* finish task and return to the kernel, replace this in p3-task2! */
    ld   t2, 0(sp)           # 恢复原sp
    ld   ra, 8(sp)           # 恢复ra
    addi sp, sp, 16
    addi sp, t2, 0           # 切回内核栈
    # jalr zero, ra, 0
    jr ra
    /************************************************************/
    /* TODO: [P3] call sys_exit */
    /************************************************************/
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)

