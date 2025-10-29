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
 

	// load task-related arguments and pass them to kernel
	la t1, app_info_addr_loc	
	lw a0, (t1)		// pass the location for task info as parameter 1
	lw a1, 4(t1)	// pass the size as parameter 2

	// jump to kernel to start UCAS-OS
	jal kernel
	/************************************************************/
	/* Do not touch this comment. Reserved for future projects. */
	/************************************************************/
// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's [who]'s bootloader...\n\r"

