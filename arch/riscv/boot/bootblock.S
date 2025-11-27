#include <asm/biosdef.h>

// os size location
.equ os_size_loc, 0x502001fc

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

// application info address location
.equ app_info_addr_loc, 0x502001f4

.text
.global main

main:
	// fence on all memory and I/O
	fence
    // a0 is mhartid
	bnez a0, secondary

/************************************************************/
/*                  主核 (Hart 0) 逻辑                      */
/************************************************************/

	// call BIOS to print string "It's bootblock!"
	la a0, msg
	li a7, BIOS_PUTSTR
	jal bios_func_entry

	// call BIOS to read kernel in SD card
	li a7, BIOS_SDREAD        # BIOS功能号：SD卡读
    la a0, kernel             # 目标内存地址
    la t0, os_size_loc        # 读取长度位置
    lh a1, 0(t0)              # 读取长度（单位：扇区），参数2
    li a2, 1                  # SD卡起始扇区（第二个扇区），参数3
    jal bios_func_entry       # 调用BIOS
	// 执行 fence.i，确保指令缓存 (I-Cache) 看到新加载的代码
	fence.i 

	# addi a0, a0, 48         # 把数字转成对应的ASCII字符
	# li   a7, BIOS_PUTCHAR   # BIOS_PUTCHAR 功能号是1
	# jal  bios_func_entry    # 调用BIOS输出字符

	// 设置标志位，通知从核内核已加载完毕
    la t0, kernel_load_completed
    li t1, 1
    sw t1, 0(t0)

	// 确保写操作被从核看到
    fence
	# // load task-related arguments and pass them to kernel
	# la t1, app_info_addr_loc	
	# lw a0, (t1)		// pass the location for task info as parameter 1
	# lw a1, 4(t1)	// pass the size as parameter 2

	// jump to kernel to start UCAS-OS
	jal kernel
	/************************************************************/
secondary:
    // 1. 【关键】从核必须等待主核加载完内核
    la t0, kernel_load_completed
wait_for_kernel:
    lw t1, 0(t0)
    beqz t1, wait_for_kernel // 如果是0，就一直在这里转圈

    // 2. 【关键】执行 fence.i，刷新自己的 I-Cache，防止读到旧的垃圾指令
    fence.i

	// 3. 安全跳转到内核
	jal kernel

wait_for_wakeup:
	wfi
	j wait_for_wakeup

	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's [who]'s bootloader...\n\r"

// 定义一个标志位变量，初始化为 0
.align 4
kernel_load_completed:
    .word 0
