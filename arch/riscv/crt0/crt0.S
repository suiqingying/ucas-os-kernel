#include <asm.h>
// # define USER_STACK      0x52500000
.section ".entry_function","ax"
ENTRY(_start)
    // 针对prj2,其实压栈过程可以省略,但是为了代码的roubustness,保留
    /* setup C runtime environment for the user program */
    # la sp, USER_STACK // wrong core reason

    la t0, __bss_start
    la t1, __BSS_END__
clear_bss:
    sw zero, 0(t0)
    addi t0, t0, 4
    bltu t0, t1, clear_bss
    /* enter main function */
    jal main                        # 跳转到用户程序main函数

    /* finish task and return to the kernel */
    /* call sys_exit */
    call sys_exit
// while(1) loop, unreachable here
loop:
    wfi
    j loop

END(_start)

