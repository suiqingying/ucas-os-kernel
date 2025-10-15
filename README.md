# Project 1: 引导、镜像文件和 ELF 文件

## 概述

本项目是国科大操作系统研讨课的第一个实验，旨在让学生从零开始构建一个精简但功能完备的操作系统。通过本实验，我们将深入理解操作系统的引导过程、镜像文件的构成、ELF 文件的结构与加载机制，并逐步实现一个能够加载并运行用户程序的引导加载器和微型内核。

**核心目标：**

1.  掌握操作系统启动的完整过程，理解软硬件、内核与应用之间的分工与约定。
2.  掌握 ELF 文件的结构和功能，以及 ELF 文件的装载。
3.  设计加载映像的索引结构。

## 项目结构

```
/home/cxh/Documents/pyx_os_lab/panyuxuan231-Project1/
├───Makefile                # 项目构建脚本
├───README.md               # 项目说明文档
├───riscv.lds               # 链接器脚本，定义内存布局和段信息
├───arch/
│   └───riscv/
│       ├───bios/
│       │   └───common.c    # BIOS API 的 C 语言封装
│       ├───boot/
│       │   └───bootblock.S # 引导程序汇编代码 (任务1, 任务2, 任务4)
│       ├───crt0/
│       │   └───crt0.S      # 用户程序 C 运行时入口 (任务3)
│       ├───include/
│       │   ├───asm.h
│       │   ├───common.h
│       │   ├───csr.h
│       │   └───asm/
│       │       └───biosdef.h # BIOS API 功能号定义
│       └───kernel/
│           └───head.S    # 内核 C 运行时入口 (任务2)
├───build/                  # 编译输出目录
├───test/                   # 用户测试程序源代码
│   └───test_project1/
│       ├───2048.c
│       ├───add10.c
│       ├───auipc.c
│       ├───bss.c
│       ├───data.c
│       ├───mul3.c
│       ├───number.c
│       └───square.c
├───include/
│   ├───type.h
│   └───os/
│       ├───kernel.h
│       ├───loader.h
│       ├───sched.h
│       ├───string.h
│       └───task.h          # 任务信息结构体定义
├───init/
│   └───main.c              # 内核主函数 (任务2, 任务4, 任务5)
├───kernel/
│   └───loader/
│       └───loader.c        # 用户程序加载器实现 (任务4, 任务5)
├───libs/
│   └───string.c            # 字符串处理库
├───tiny_libc/              # 为用户程序提供的超小型 C 库
│   └───include/
│       ├───batch.h
│       └───kernel.h
└───tools/
    └───createimage.c       # 镜像制作工具源代码 (任务3, 任务4, 任务5)
```

## 实验环境

*   **操作系统:** Linux
*   **CPU 架构:** RISC-V
*   **模拟器:** QEMU
*   **开发板 (可选):** PYNQ

## 任务详情与实现

### 任务 1：第一个引导块的制作 (S/A/C-Core)

**目标：** 了解操作系统引导块的加载过程，编写 Boot Block，调用 BIOS 中的输入输出函数，在终端成功输出指定字符串。

**关键概念：**

*   **引导 (Booting):** CPU 上电后，PC 寄存器被设置到固定地址，执行 ROM 中的程序 (BIOS)。BIOS 将存储设备第一个扇区 (512B) 的数据加载到内存指定位置 (`0x50200000`)，然后跳转执行，此数据即为 Boot Loader。
*   **BIOS API 调用:** 通过汇编语言调用 BIOS 提供的服务。函数编号放入 `a7` 寄存器，参数依次放入 `a0`, `a1` 等寄存器，然后执行 `jal bios_func_entry`。`arch/riscv/include/asm/biosdef.h` 中定义了可调用的接口编号。

**实现细节：**

修改 `arch/riscv/boot/bootblock.S` 文件，添加代码以调用 `BIOS_PUTSTR` 函数打印字符串 "It's [who]'s bootloader..."。

**`arch/riscv/boot/bootblock.S` 片段:**

```assembly
#include <asm/biosdef.h>

// os size location (os_size could be sectors [p1-task3] or bytes [p1-task4])
.equ os_size_loc, 0x502001fc

// kernel address (move kernel to here, and jmp here to start kernel)
.equ kernel, 0x50201000

// BIOS function entry (jump here to use BIOS APIs)
.equ bios_func_entry, 0x50150000

// application info address location (for p1-task4)
.equ app_info_addr_loc, 0x502001f4

.text
.global main

main:
	// fence on all memory and I/O
	fence

	// TODO: [p1-task1] call BIOS to print string "It's bootblock!"
	la a0, msg
	li a7, BIOS_PUTSTR
	jal bios_func_entry

    // ... (任务2和任务4的代码将在此处继续)

// while(1) --> stop here
stop:
	j stop
	nop

.data

msg: .string "It's [who]'s bootloader...

"
```

**验证：**

1.  运行 `make dirs`。
2.  运行 `make elf`。
3.  将提供的 `createimage` 可执行文件复制到 `build` 目录。
4.  在 `build` 目录下执行 `./createimage --extended bootblock main` 生成 `image` 文件。
5.  运行 `make run` 启动 QEMU，观察终端输出。

### 任务 2：加载和初始化内存 (S/A/C-Core)

**目标：** 调用 BIOS API 从 SD 卡中加载内核，完成内存空间的初始化，并跳转至内核执行，在进入内核之后成功打印出 "Hello OS"，并能接收键盘输入并回显。

**关键概念：**

*   **内核加载:** Boot Loader 将内核代码从 SD 卡的第二个扇区开始，拷贝到内存地址 `0x50201000`。内核大小信息存储在 `0x502001fc`。
*   **C 语言运行环境初始化:** 内核和用户程序在执行 C 语言代码前，需要汇编代码完成 BSS 段清零和栈指针设置。
*   **BSS 段:** 存放未初始化或初始化为 0 的全局变量和静态变量。在程序启动时需要清零。`riscv.lds` 中定义了 `__bss_start` 和 `__BSS_END__` 符号。
*   **栈:** 用于函数调用、局部变量存储等。需要设置栈顶地址。内核栈顶地址定义为 `KERNEL_STACK` (`0x50500000`)。

**实现细节：**

1.  **`arch/riscv/boot/bootblock.S`:**
    *   读取 `os_size_loc` (`0x502001fc`) 处的内核扇区数。
    *   调用 `BIOS_SDREAD` 将内核从 SD 卡的第二个扇区 (block ID 为 1) 开始，加载到 `kernel` (`0x50201000`)。
    *   跳转到 `kernel` (`0x50201000`)。

    **`arch/riscv/boot/bootblock.S` 片段:**

    ```assembly
    // ... (任务1代码)

	// TODO: [p1-task2] call BIOS to read kernel in SD card
	li a7, BIOS_SDREAD        # BIOS功能号：SD卡读
    la a0, kernel             # 目标内存地址
    la t0, os_size_loc        # 读取长度位置
    lh a1, 0(t0)              # 读取长度（单位：扇区），参数2
    li a2, 1                  # SD卡起始扇区（第二个扇区），参数3
    jal bios_func_entry       # 调用BIOS
 
    // ... (任务4的代码将在此处继续)

	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	jal kernel
    // ... (stop 标签)
    ```

2.  **`arch/riscv/kernel/head.S`:**
    *   清零 BSS 段：使用 `__bss_start` 和 `__BSS_END__` 符号。
    *   设置栈指针：将栈顶地址设置为 `KERNEL_STACK` (`0x50500000`)。
    *   跳转到 `main.c` 中的 `main` 函数。

    **`arch/riscv/kernel/head.S` 片段:**

    ```assembly
    #include <asm.h>
    #include <csr.h>

    #define KERNEL_STACK		0x50500000

    .section ".entry_function","ax"
    ENTRY(_start)
      /* Mask all interrupts */
      csrw CSR_SIE, zero
      csrw CSR_SIP, zero

      /* TODO: [p1-task2] clear BSS for flat non-ELF images */
      la t0, __bss_start
      la t1, __BSS_END__  
      clear_bss:
        sw zero, 0(t0)
        addi t0, t0, 4
        bltu t0, t1, clear_bss

      /* TODO: [p1-task2] setup C environment */
      bss_done:
        la sp, KERNEL_STACK
        j main

    loop:
      wfi
      j loop

    END(_start)
    ```

3.  **`init/main.c`:**
    *   调用 `init_jmptab()` 初始化跳转表。
    *   调用 `init_task_info()` 初始化任务信息 (此函数在任务4中实现)。
    *   打印 "Hello OS!"。
    *   打印 "bss check: t version: X" (用于验证 BSS 清零)。
    *   进入循环，提示用户输入任务名称，并调用 `load_task_img` (任务4/5实现) 加载并执行。

    **`init/main.c` 片段:**

    ```c
    #include <common.h>
    #include <asm.h>
    #include <os/kernel.h>
    #include <os/task.h>
    #include <os/string.h>
    #include <os/loader.h>
    #include <type.h>

    #define VERSION_BUF 50

    int version = 2; // version must between 0 and 9
    char buf[VERSION_BUF];

    // Task info array
    task_info_t tasks[TASK_MAXNUM];

    static int bss_check(void)
    {
        for (int i = 0; i < VERSION_BUF; ++i)
        {
            if (buf[i] != 0)
            {
                return 0;
            }
        }
        return 1;
    }

    static void init_jmptab(void)
    {
        volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

        jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
        jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
        jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
        jmptab[SD_READ]         = (long (*)())sd_read;
        jmptab[SD_WRITE]        = (long (*)())sd_write;
    }

    static void init_task_info(int app_info_loc, int app_info_size)
    {
        // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
        // NOTE: You need to get some related arguments from bootblock first
        int start_sec, blocknums;
        start_sec = app_info_loc / SECTOR_SIZE;
        blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sec;
        int task_info_addr = TASK_INFO_MEM;
        bios_sd_read(task_info_addr, blocknums, start_sec);
        int start_addr = (TASK_INFO_MEM + app_info_loc - start_sec * SECTOR_SIZE);
        memcpy((uint8_t *)tasks, (uint8_t *)start_addr, app_info_size);
    }

    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    int main(int app_info_loc, int app_info_size)
    {
        // Check whether .bss section is set to zero
        int check = bss_check();

        // Init jump table provided by kernel and bios(ΦωΦ)
        init_jmptab();

        // Init task information (〃'▽'〃)
        init_task_info(app_info_loc, app_info_size);

        // Output 'Hello OS!', bss check result and OS version
        char output_str[] = "bss check: _ version: _";
        char output_val[2] = {0};
        int i, output_val_pos = 0;

        output_val[0] = check ? 't' : 'f';
        output_val[1] = version + '0';
        for (i = 0; i < sizeof(output_str); ++i)
        {
            buf[i] = output_str[i];
            if (buf[i] == '_')
            {
                buf[i] = output_val[output_val_pos++];
            }
        }

        bios_putstr("Hello OS!");
        bios_putstr(buf);

        while (1) {
            bios_putstr("Please input task name:");

            char input_name[16] = "";
            int idx = 0;
            int ch;
            // 读取字符串直到回车
            while ((ch = bios_getchar()) != ' ' && idx < 31) {
                if (ch != -1) {
                    input_name[idx++] = (char)ch;
                    bios_putchar(ch); // 回显
                }
            }
            input_name[idx] = '\0';

            uint64_t entry_addr = load_task_img(input_name);
            bios_putstr(" ");
            if(entry_addr != 0){
                void (*entry)(void) = (void (*)(void))entry_addr;
                entry();
                bios_putstr("Task loaded. You can input again.");
            }
        }
        
        // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
        while (1)
        {
            asm volatile("wfi");
        }

        return 0;
    }
    ```

**验证：**

1.  运行 `make dirs`。
2.  运行 `make elf`。
3.  将提供的 `createimage` 可执行文件复制到 `build` 目录。
4.  在 `build` 目录下执行 `./createimage --extended bootblock main` 生成 `image` 文件。
5.  运行 `make run` 启动 QEMU，观察终端输出 "Hello OS!" 和 "bss check..."。

### 任务 3：加载并选择启动多个用户程序之一 (S/A/C-Core)

**目标：** 填写 `createimage.c` 实现镜像制作，在内核中实现交互式地输入数字 (task id) 选择运行用户程序，并实现用户程序的加载和 C 语言运行环境初始化。

**注意：** 原始 `main.c` 中任务3的交互式输入代码已被任务4的按名称加载代码替换并注释。本任务的实现细节将基于原始任务3的描述，但实际运行将受 `main.c` 现有逻辑影响。

**关键概念：**

*   **镜像文件组成:** Boot Loader (第一个扇区), Kernel (第二个扇区开始), 用户程序 (App1, App2...)。
*   **`createimage` 工具:** 解析 ELF 文件，合并其段到最终的 `image` 文件。
*   **用户程序加载:** 将用户程序从镜像文件拷贝到内存指定区域 (例如 `TASK_MEM_BASE + taskid * TASK_SIZE`)。
*   **用户程序 C 运行时初始化:** `crt0.S` 为用户程序设置栈和清零 BSS 段，然后跳转到用户程序的 `main` 函数。用户程序执行完毕后，通过 `jalr zero, ra, 0` 返回到调用者 (内核)。

**实现细节：**

1.  **`tools/createimage.c`:**
    *   `write_img_info()` 函数负责将内核所占扇区数 (`kernel_sectors`) 和用户程序数目 (`tasknum`) 写入 `OS_SIZE_LOC` (`0x502001fc`) 及其后位置。
    *   在 `create_image` 函数中，为 `bootblock` 写入 `SECTOR_SIZE` 的填充。对于 `main` 和用户程序，`createimage` 会计算其在镜像中的实际大小，并填充到 2 字节对齐。同时，它会填充 `taskinfo` 数组，记录每个用户程序的名称、起始地址和块数。

    **`tools/createimage.c` 片段:**

    ```c
    // ... (头文件和宏定义)

    #define OS_SIZE_LOC (BOOT_LOADER_SIG_OFFSET - 2) // 0x1fc
    #define APP_INFO_ADDR_LOC (BOOT_LOADER_SIG_OFFSET - 10) // 0x1f4

    /* TODO: [p1-task4] design your own task_info_t */
    typedef struct {
        char task_name[16];
        int start_addr;     // 在镜像文件中的起始地址 (字节偏移)
        int block_nums;     // 占用的扇区数
    } task_info_t;

    #define TASK_MAXNUM 16
    static task_info_t taskinfo[TASK_MAXNUM];

    // ... (main 函数和辅助函数)

    static void create_image(int nfiles, char *files[])
    {
        int tasknum = nfiles - 2; // 实际的用户程序数量
        int nbytes_kernel = 0;
        int phyaddr = 0; // 当前写入的物理地址偏移
        FILE *fp = NULL, *img = NULL;
        Elf64_Ehdr ehdr;
        Elf64_Phdr phdr;

        /* open the image file */
        img = fopen(IMAGE_FILE, "w");
        assert(img != NULL);

        /* for each input file */
        for (int fidx = 0; fidx < nfiles; ++fidx) {

            int taskidx = fidx - 2; // 用户程序在 taskinfo 数组中的索引
            int start_addr = phyaddr; // 当前文件的起始物理地址

            /* open input file */
            fp = fopen(*files, "r");
            assert(fp != NULL);

            /* read ELF header */
            read_ehdr(&ehdr, fp);
            printf("0x%04lx: %s", ehdr.e_entry, *files);

            /* for each program header */
            for (int ph = 0; ph < ehdr.e_phnum; ph++) {

                /* read program header */
                read_phdr(&phdr, fp, ph, ehdr);

                if (phdr.p_type != PT_LOAD) continue;

                /* write segment to the image */
                write_segment(phdr, fp, img, &phyaddr);

                /* update nbytes_kernel */
                if (strcmp(*files, "main") == 0) {
                    nbytes_kernel += get_filesz(phdr);
                }
            }

            /* write padding bytes */
            if (strcmp(*files, "bootblock") == 0) {
                write_padding(img, &phyaddr, SECTOR_SIZE); // bootblock 填充到 1 扇区
            } else {
                // [p1-task4] 仅对齐到 2 字节，实现紧密排列
                write_padding(img, &phyaddr, phyaddr + (phyaddr & 1)); // 2字节对齐
                // 填充 taskinfo 结构体
                strcpy(taskinfo[taskidx].task_name, *files);
                taskinfo[taskidx].start_addr = start_addr;
                taskinfo[taskidx].block_nums  = NBYTES2SEC(phyaddr) - start_addr / SECTOR_SIZE;
                printf("current phyaddr:%x", phyaddr);
                printf("%s: start_addr is %x, blocknums is %d", taskinfo[taskidx].task_name, taskinfo[taskidx].start_addr,taskinfo[taskidx].block_nums);
            }
            fclose(fp);
            files++;
        }
        // 写入镜像信息 (包括内核大小和任务信息)
        write_img_info(nbytes_kernel, taskinfo, tasknum, img, &phyaddr);
        printf("current phyaddr:%x", phyaddr);
        // 填充到扇区边界
        fseek(img, phyaddr, SEEK_SET);
        write_padding(img, &phyaddr, NBYTES2SEC(phyaddr) * SECTOR_SIZE);
        printf("current phyaddr:%x", phyaddr);
        // 为批处理文件预留空间
        write_padding(img, &phyaddr, (BATCH_FILE_SECTOR + 3) * SECTOR_SIZE);
        printf("Writing padding for batch file, current phyaddr:%x", phyaddr);
        fclose(img);
    }

    // ... (read_ehdr, read_phdr, get_entrypoint, get_filesz, get_memsz, write_segment, write_padding)

    static void write_img_info(int nbytes_kernel, task_info_t *taskinfo,
                           short tasknum, FILE * img, int *taskinfo_addr)
    {
        // TODO: [p1-task3] & [p1-task4] write image info to some certain places
        // NOTE: os size, infomation about app-info sector(s) ...
        // 计算 kernel 所占扇区数
        short kernel_sectors = NBYTES2SEC(nbytes_kernel);

        // 跳转到 OS_SIZE_LOC 位置 (0x1fc)
        fseek(img, OS_SIZE_LOC, SEEK_SET);

        // 写入 kernel 所占扇区数（2字节）
        fwrite(&kernel_sectors, sizeof(short), 1, img);
        printf("kernel sectors: %d", kernel_sectors); 

        // 写入用户程序数目（2字节，紧跟在 kernel_sectors 后面）
        fwrite(&tasknum, sizeof(short), 1, img);
        printf("user program count: %d", tasknum);

        // 将taskinfo的size写进bootloader的末尾几个字节
        int info_size = sizeof(task_info_t) * tasknum;
        // 将定位信息写进bootloader的末尾几个字节
        fseek(img, APP_INFO_ADDR_LOC, SEEK_SET);  // 文件指针指到 APP_INFO_ADDR_LOC (0x1f4)
        fwrite(taskinfo_addr, sizeof(int), 1, img); // 写入 taskinfo 数组的起始物理地址
        printf("Task info address: %x", *taskinfo_addr);
        fwrite(&info_size, sizeof(int), 1, img);    // 写入 taskinfo 数组的总大小
        printf("Task info size: %d", info_size);
        fseek(img, *taskinfo_addr, SEEK_SET);  // 跳转到 taskinfo 数组的实际写入位置
        fwrite(taskinfo, sizeof(task_info_t), tasknum, img); // 写入 taskinfo 数组
        printf("Task info written at: %x", *taskinfo_addr);
        *taskinfo_addr += info_size; // 更新 phyaddr
    }

    // ... (error 函数)
    ```

2.  **`arch/riscv/crt0/crt0.S`:**
    *   为用户程序提供 C 语言运行环境。
    *   清零 BSS 段：使用 `__bss_start` 和 `__BSS_END__` 符号。
    *   设置栈指针：将栈顶地址设置为 `USER_STACK` (`0x52500000`)。
    *   调用用户程序的 `main` 函数。
    *   用户程序 `main` 返回后，通过 `jalr zero, ra, 0` 返回到调用者 (内核)。

    **`arch/riscv/crt0/crt0.S` 片段:**

    ```assembly
    #include <asm.h>
    #define USER_STACK      0x52500000
    .section ".entry_function","ax"
    ENTRY(_start)

        /* TODO: [p1-task3] setup C runtime environment for the user program */
        addi t2, sp, 0           # 保存当前 sp (内核栈)
        la sp, USER_STACK        # 设置用户栈顶
        addi sp, sp, -16         # 分配空间
        sd   t2, 0(sp)           # 压栈保存原sp
        sd   ra, 8(sp)           # 压栈保存ra

        la t0, __bss_start       # BSS 段起始地址
        la t1, __BSS_END__       # BSS 段结束地址
    clear_bss:
        sw zero, 0(t0)           # 清零 BSS
        addi t0, t0, 4
        bltu t0, t1, clear_bss
        /* TODO: [p1-task3] enter main function */
        jal main                 # 跳转到用户程序main函数

        /* TODO: [p1-task3] finish task and return to the kernel, replace this in p3-task2! */
        ld   t2, 0(sp)           # 恢复原sp
        ld   ra, 8(sp)           # 恢复ra
        addi sp, sp, 16
        addi sp, t2, 0           # 切回内核栈
        jalr zero, ra, 0         # 返回到调用者 (内核)
        /************************************************************/
    	/* Do not touch this comment. Reserved for future projects. */
    	/************************************************************/
    // while(1) loop, unreachable here
    loop:
        wfi
        j loop

    END(_start)
    ```

**验证：**

1.  运行 `make all` (这将使用您自己编写的 `createimage` 生成 `image` 文件)。
2.  运行 `make run` 启动 QEMU。由于 `main.c` 已经更新为任务4的按名称加载逻辑，您将无法直接通过 task id 交互。但 `createimage` 和 `crt0.S` 的功能已实现。

### 任务 4：镜像文件的紧密排列 (A/C-Core)

**目标：** 优化镜像文件布局，实现内核与用户程序、用户程序之间紧密排列，消除“空泡”。通过用户程序名称 (name) 交互式加载并启动用户程序。为内核和用户程序编译启用 `-O2` 优化选项。

**关键概念：**

*   **紧密排列:** 根据 ELF 文件的实际大小而非固定扇区数来存储内核和用户程序，减少镜像文件大小。`createimage.c` 中通过 2 字节对齐实现。
*   **`task_info_t` 结构体:** 定义在 `include/os/task.h` 中，用于存储每个用户程序的元数据，包括程序名称 (`task_name`)、在镜像文件中的起始地址 (`start_addr`) 和占用的扇区数 (`block_nums`)。这些信息由 `createimage` 写入镜像文件，供内核读取。
*   **按名称加载:** 内核根据用户输入的程序名称，查找对应的 `task_info_t` 结构体，然后加载并执行。
*   **`-O2` 优化:** 编译器优化选项，有助于发现潜在的 bug 并提高代码性能。在 `Makefile` 中，`CFLAGS` 已经包含了 `-O2`，并被 `KERNEL_CFLAGS` 和 `USER_CFLAGS` 继承。

**实现细节：**

1.  **`include/os/task.h`:**
    *   定义 `task_info_t` 结构体。

    **`include/os/task.h` 片段:**

    ```c
    #ifndef __INCLUDE_TASK_H__
    #define __INCLUDE_TASK_H__

    #include <type.h>

    #define TASK_MEM_BASE    0x52000000 // 用户程序加载的基地址
    #define TASK_MAXNUM      16         // 最大任务数
    #define TASK_SIZE        0x10000    // 每个任务预留的内存大小 (64KB)
    #define TASK_INFO_MEM    0x52300000 // 任务信息加载到内存的地址

    #define SECTOR_SIZE 512
    #define NBYTES2SEC(nbytes) (((nbytes) / SECTOR_SIZE) + ((nbytes) % SECTOR_SIZE != 0))

    /* TODO: [p1-task4] implement your own task_info_t! */
    typedef struct {
        char task_name[16]; // 用户程序名称
        int start_addr;     // 在镜像文件中的起始地址 (字节偏移)
        int block_nums;     // 占用的扇区数
    } task_info_t;  

    extern task_info_t tasks[TASK_MAXNUM]; // 全局任务信息数组

    #endif
    ```

2.  **`tools/createimage.c`:**
    *   `create_image` 函数在写入 `bootblock` 后的文件时，会填充 `taskinfo` 数组。`taskinfo[taskidx].start_addr` 记录了文件在镜像中的字节偏移，`taskinfo[taskidx].block_nums` 记录了其占用的扇区数。
    *   `write_img_info` 函数将 `kernel_sectors`、`tasknum`、`taskinfo` 数组的起始物理地址 (`*taskinfo_addr`) 和总大小 (`info_size`) 写入 `bootblock` 扇区末尾的特定位置 (`OS_SIZE_LOC`, `APP_INFO_ADDR_LOC`)。然后将 `taskinfo` 数组本身写入 `*taskinfo_addr` 指定的物理地址。

    **`tools/createimage.c` 片段 (已在任务3中展示，此处不再重复)**

3.  **`arch/riscv/boot/bootblock.S`:**
    *   在跳转到内核之前，从 `APP_INFO_ADDR_LOC` (`0x502001f4`) 读取 `taskinfo` 数组的起始地址和大小，并作为参数传递给内核的 `main` 函数。

    **`arch/riscv/boot/bootblock.S` 片段:**

    ```assembly
    // ... (任务1和任务2代码)

	// TODO: [p1-task4] load task-related arguments and pass them to kernel
	la t1, app_info_addr_loc	
	lw a0, (t1)		// pass the location for task info as parameter 1 (app_info_loc)
	lw a1, 4(t1)	// pass the size as parameter 2 (app_info_size)

	// TODO: [p1-task2] jump to kernel to start UCAS-OS
	jal kernel
    // ... (stop 标签)
    ```

4.  **`init/main.c`:**
    *   `main` 函数接收 `app_info_loc` 和 `app_info_size` 作为参数。
    *   调用 `init_task_info(app_info_loc, app_info_size)`，该函数会从 SD 卡读取 `taskinfo` 数组到内存 `TASK_INFO_MEM` (`0x52300000`)，并复制到全局 `tasks` 数组。
    *   进入循环，提示用户输入任务名称，并调用 `loader.c` 中的 `load_task_img(char *input_name)` 函数加载并执行。

    **`init/main.c` 片段 (已在任务2中展示，此处不再重复)**

5.  **`kernel/loader/loader.c`:**
    *   `load_task_img(char *input_line)` 函数解析用户输入，如果是任务名称，则调用 `load_single_task(char *task_name)`。
    *   `load_single_task(char *task_name)` 遍历全局 `tasks` 数组，根据 `task_name` 查找对应的 `task_info_t`。
    *   找到后，计算用户程序在内存中的加载地址 (`TASK_MEM_BASE + TASK_SIZE * i`)，并根据 `task_info_t` 中的 `start_addr` 和 `block_nums` 调用 `bios_sd_read` 将程序加载到内存。

    **`kernel/loader/loader.c` 片段:**

    ```c
    #include <os/task.h>
    #include <os/string.h>
    #include <os/kernel.h>
    #include <type.h>
    #define PIPE_LOC 0x54000000 /* address of pipe */
    #define BATCH_FILE_SECTOR 50
    #define BATCH_FILE_MAX_TASKS 16
    #define BATCH_FILE_TASK_NAME_LEN 16

    // ... (函数声明)

    uint64_t load_task_img(char *input_line)
    {
        /**
         * TODO:
         * 1. [p1-task3] load task from image via task id, and return its entrypoint
         * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
         */
        char cmd[32] = {0};
        char args[128] = {0};
        int i = 0;
        // 提取命令
        while (input_line[i] != ' ' && input_line[i] != '\0') {
            cmd[i] = input_line[i];
            i++;
        }
        cmd[i] = '\0';
        // 提取参数
        if (input_line[i] == ' ') {
            strncpy(args, input_line + i + 1, sizeof(args) - 1);
        }

        if (strcmp(cmd, "batch_write") == 0) {
            char *task_list[BATCH_FILE_MAX_TASKS];
            int task_count = parse_task_names(args, task_list);
            write_batch_file(task_list, task_count);
            return 0;
        }
        
        if (strcmp(cmd, "batch_run") == 0) {
            batch_process_from_file();
            return 0;
        }

        if (strcmp(cmd, "list") == 0) {
            list_user_tasks(); // 列出用户程序
            return 0; // 列出任务不需要返回入口地址
        }
        // 普通任务名
        return load_single_task(cmd);
    }

    uint64_t load_single_task(char *task_name)
    {
        for (int i = 0; i < TASK_MAXNUM; i++)
        {
            if (strcmp(task_name, tasks[i].task_name) == 0)
            {
                uint64_t mem_addr = TASK_MEM_BASE + TASK_SIZE * i; // 计算内存加载地址
                int start_sec = tasks[i].start_addr / 512; // 计算在镜像中的起始扇区
                bios_sd_read(mem_addr, tasks[i].block_nums, start_sec); // 从SD卡读取
                return mem_addr + (tasks[i].start_addr - start_sec * 512); // 返回实际入口地址
            }
        }
        bios_putstr("Fail to find the task! Please try again!");
        return 0;
    }

    // ... (list_user_tasks, write_batch_file, read_batch_file, batch_process_from_file, parse_task_names)
    ```

6.  **`Makefile`:**
    *   `CFLAGS` 变量已经包含了 `-O2` 优化选项。`KERNEL_CFLAGS` 和 `USER_CFLAGS` 继承了 `CFLAGS`，因此内核和用户程序默认会以 `-O2` 优化编译。

    **`Makefile` 片段:**

    ```makefile
    # ... (其他定义)

    CFLAGS          = -O2 -std=gnu11 -fno-builtin -nostdlib -nostdinc -Wall -mcmodel=medany -ggdb3

    BOOT_INCLUDE    = -I$(DIR_ARCH)/include
    BOOT_CFLAGS     = $(CFLAGS) $(BOOT_INCLUDE) -Wl,--defsym=TEXT_START=$(BOOTLOADER_ENTRYPOINT) -T riscv.lds

    KERNEL_INCLUDE  = -I$(DIR_ARCH)/include -Iinclude
    KERNEL_CFLAGS   = $(CFLAGS) $(KERNEL_INCLUDE) -Wl,--defsym=TEXT_START=$(KERNEL_ENTRYPOINT) -T riscv.lds

    USER_INCLUDE    = -I$(DIR_TINYLIBC)/include
    USER_CFLAGS     = $(CFLAGS) $(USER_INCLUDE)

    # ... (其他规则)
    ```

**验证：**

1.  运行 `make all`。
2.  运行 `make run` 启动 QEMU，尝试输入用户程序名称来加载并执行。

### 任务 5：列出用户程序和批处理 (C-Core)

**目标：** 实现简单的批处理系统，支持列出所有用户程序名称，写入批处理文件，并按批处理文件定义的顺序执行多个用户程序，程序之间可以传递输入输出。批处理文件应存储在镜像文件中，重启后仍可读写。

**关键概念：**

*   **列出用户程序:** 遍历全局 `tasks` 数组，打印所有程序名称。
*   **批处理:** 顺序执行多个用户程序。用户程序执行完毕后，控制流返回内核，内核再启动下一个程序。
*   **程序间输入输出传递:** 约定固定的内存地址 (`PIPE_LOC`, `0x54000000`) 作为数据缓冲区，用于传递数字。
*   **批处理文件:** 存储在镜像文件中的持久化数据，定义批处理序列。`BATCH_FILE_SECTOR` (`50`) 定义了其在 SD 卡中的起始扇区。使用 `bios_sd_write` 和 `bios_sd_read` 进行读写。
*   **持久化存储:** `createimage.c` 在镜像文件末尾为批处理文件预留了空间。

**实现细节：**

1.  **`init/main.c`:**
    *   `main` 函数中的 `while(1)` 循环会提示用户输入命令或任务名称。
    *   如果输入 `list`，则调用 `loader.c` 中的 `list_user_tasks()`。
    *   如果输入 `batch_write <task_list>`，则调用 `loader.c` 中的 `write_batch_file()`。
    *   如果输入 `batch_run`，则调用 `loader.c` 中的 `batch_process_from_file()`。
    *   用户程序执行完毕后，`crt0.S` 会返回到内核，`main` 函数的循环会继续，从而实现批处理的顺序执行。

    **`init/main.c` 片段 (已在任务2中展示，此处不再重复)**

2.  **`kernel/loader/loader.c`:**
    *   **`list_user_tasks()`:** 遍历全局 `tasks` 数组，打印每个任务的 `task_name`。
    *   **`write_batch_file(char *task_list[], int task_count)`:** 将批处理序列 (任务名称列表) 写入一个缓冲区，然后使用 `sd_write` 将缓冲区内容写入 SD 卡的 `BATCH_FILE_SECTOR`。
    *   **`read_batch_file(char task_list[][BATCH_FILE_TASK_NAME_LEN])`:** 使用 `sd_read` 从 `BATCH_FILE_SECTOR` 读取批处理文件内容到缓冲区，然后解析出任务名称列表。
    *   **`batch_process_from_file()`:** 调用 `read_batch_file` 获取批处理序列，然后循环遍历序列中的每个任务名称，调用 `load_single_task` 加载并执行。
    *   **`parse_task_names(char *args, char *task_list[])`:** 辅助函数，用于从字符串中解析出任务名称列表。

    **`kernel/loader/loader.c` 片段:**

    ```c
    #include <os/task.h>
    #include <os/string.h>
    #include <os/kernel.h>
    #include <type.h>
    #define PIPE_LOC 0x54000000 /* address of pipe */
    #define BATCH_FILE_SECTOR 50
    #define BATCH_FILE_MAX_TASKS 16
    #define BATCH_FILE_TASK_NAME_LEN 16

    // ... (load_task_img, load_single_task 函数)

    void list_user_tasks()
    {
        bios_putstr("User programs:");
        for (int i = 0; i < TASK_MAXNUM; i++) {
            if (strlen(tasks[i].task_name) > 0) { // 检查任务名是否非空
                bios_putstr(tasks[i].task_name);
                bios_putstr(" ");
            }
        }
    }

    // 写入批处理文件
    void write_batch_file(char *task_list[], int task_count) {
        char buffer[BATCH_FILE_MAX_TASKS * BATCH_FILE_TASK_NAME_LEN] = {0};
        for (int i = 0; i < task_count && i < BATCH_FILE_MAX_TASKS; i++) {
            strncpy(buffer + i * BATCH_FILE_TASK_NAME_LEN, task_list[i], BATCH_FILE_TASK_NAME_LEN - 1);
        }
        sd_write((uintptr_t)buffer, 1, BATCH_FILE_SECTOR); // 写入一个扇区
        bios_putstr("Batch file written.");
    }

    // 读取批处理文件
    int read_batch_file(char task_list[][BATCH_FILE_TASK_NAME_LEN]) {
        char buffer[512] = {0};
        sd_read((uintptr_t)buffer, 1, BATCH_FILE_SECTOR); // 读取一个扇区
        int count = 0;
        for (int i = 0; i < BATCH_FILE_MAX_TASKS; i++) {
            if (buffer[i * BATCH_FILE_TASK_NAME_LEN] != 0) { // 检查任务名是否非空
                strncpy(task_list[count], buffer + i * BATCH_FILE_TASK_NAME_LEN, BATCH_FILE_TASK_NAME_LEN);
                count++;
            }
        }
        return count;
    }

    void batch_process_from_file() {
        char task_list[BATCH_FILE_MAX_TASKS][BATCH_FILE_TASK_NAME_LEN] = {0};
        int task_count = read_batch_file(task_list);
        bios_putstr("Batch executing:");
        for (int i = 0; i < task_count; i++) {
            bios_putstr("Running: ");
            bios_putstr(task_list[i]);
            bios_putstr(" ");
            uint64_t entry_addr = load_single_task(task_list[i]);
            if (entry_addr != 0) {
                void (*entry)(void) = (void (*)(void))entry_addr;
                entry(); // 执行用户程序
                bios_putstr("Task finished.");
            } else {
                bios_putstr("Task not found!");
            }
        }
    }

    int parse_task_names(char *args, char *task_list[]) {
        int count = 0;
        char *p = args;
        while (*p && count < BATCH_FILE_MAX_TASKS) {
            // 跳过前导空格
            while (*p == ' ') p++;
            if (!*p) break;
            // 记录任务名起始
            task_list[count++] = p;
            // 找到下一个空格并替换为 '\0'
            while (*p && *p != ' ') p++;
            if (*p) {
                *p = '\0';
                p++;
            }
        }
        return count;
    }
    ```

3.  **`arch/riscv/crt0/crt0.S`:**
    *   用户程序执行完毕后，通过 `jalr zero, ra, 0` 返回到调用者 (内核 `main` 函数的循环)。

    **`arch/riscv/crt0/crt0.S` 片段 (已在任务3中展示，此处不再重复)**

4.  **`tools/createimage.c`:**
    *   在 `create_image` 函数的末尾，为批处理文件预留了 `(BATCH_FILE_SECTOR + 3) * SECTOR_SIZE` 的空间。

    **`tools/createimage.c` 片段 (已在任务3中展示，此处不再重复)**

**验证：**

1.  运行 `make all`。
2.  运行 `make run` 启动 QEMU。
3.  尝试 `list` 命令查看用户程序。
4.  尝试 `batch_write number add10 mul3 square` 命令写入一个批处理序列。
5.  尝试 `batch_run` 命令执行批处理。观察程序间的输入输出传递是否正确，以及程序执行完毕后是否能返回内核并继续下一个批处理任务。

---

## 构建与运行

1.  **创建构建目录:**
    ```bash
    make dirs
    ```
2.  **编译所有 ELF 文件 (内核和用户程序):**
    ```bash
    make elf
    ```
3.  **使用自定义 `createimage` 工具生成镜像文件:**
    ```bash
    make all
    ```
    这将使用 `tools/createimage.c` 编译出的 `createimage` 工具来生成 `build/image` 文件。
4.  **在 QEMU 中运行:**
    ```bash
    make run
    ```
    在 QEMU 启动后，根据任务要求输入 `loadboot` 命令 (如果 BBL 需要) 或直接与内核交互。

5.  **在 PYNQ 开发板上运行 (可选，需要板卡):**
    *   **写入 SD 卡:**
        ```bash
        make floppy
        ```
    *   **监视串口输出并重启开发板:**
        ```bash
        make minicom
        ```
        然后重启您的 PYNQ 开发板，观察串口输出。

## 注意事项

*   **内存地址:** 密切关注内存布局和地址分配，特别是 Boot Loader、内核、用户程序以及栈和 BSS 段的地址。
*   **BIOS API:** 熟悉 `arch/riscv/include/asm/biosdef.h` 中定义的 BIOS API 函数编号和参数约定。
*   **ELF 文件结构:** 理解 ELF 文件的段 (sections) 和程序头 (program headers) 对于正确解析和加载至关重要。
*   **`createimage`:** 您的 `createimage` 工具是核心，需要精确计算文件偏移量和大小。
*   **汇编语言:** `bootblock.S`, `head.S`, `crt0.S` 需要使用 RISC-V 汇编语言编写，注意伪指令的使用和常量加载方式。
*   **错误处理:** 在实际项目中，需要考虑更多的错误处理机制，例如无效的 task ID 或文件读取失败。

---