#include <common.h>
#include <asm.h>
#include <asm/unistd.h>
#include <os/loader.h>
#include <os/irq.h>
#include <os/sched.h>
#include <os/lock.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/mm.h>
#include <os/time.h>
#include <sys/syscall.h>
#include <screen.h>
#include <printk.h>
#include <assert.h>
#include <type.h>
#include <csr.h>

extern void ret_from_exception();

// Task info array
task_info_t tasks[TASK_MAXNUM];


static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
    jmptab[QEMU_LOGGING]    = (long (*)())qemu_logging;
    jmptab[SET_TIMER]       = (long (*)())set_timer;
    jmptab[READ_FDT]        = (long (*)())read_fdt;
    jmptab[MOVE_CURSOR]     = (long (*)())screen_move_cursor;
    jmptab[PRINT]           = (long (*)())printk;
    jmptab[YIELD]           = (long (*)())do_scheduler;
    jmptab[MUTEX_INIT]      = (long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ]       = (long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE]   = (long (*)())do_mutex_lock_release;

    // TODO: [p2-task1] (S-core) initialize system call table.

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
static void init_pcb_stack(
    ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point,
    pcb_t *pcb)
{
     /* TODO: [p2-task3] initialization of registers on kernel stack
      * HINT: sp, ra, sepc, sstatus
      * NOTE: To run the task in user mode, you should set corresponding bits
      *     of sstatus(SPP, SPIE, etc.).
      */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));


    /* TODO: [p2-task1] set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));

}

static void init_pcb(void)
{
    /* TODO: [p2-task1] load needed tasks and init their corresponding PCB */


    /* TODO: [p2-task1] remember to initialize 'current_running' */

}

static void init_syscall(void)
{
    // TODO: [p2-task3] initialize system call table.
}
/************************************************************/

int main(int app_info_loc, int app_info_size)
{
    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info(app_info_loc, app_info_size);

    // Init Process Control Blocks |•'-'•) ✧
    init_pcb();
    printk("> [INIT] PCB initialization succeeded.\n");

    // Read CPU frequency (｡•ᴗ-)_
    time_base = bios_read_fdt(TIMEBASE);

    // Init lock mechanism o(´^｀)o
    init_locks();
    printk("> [INIT] Lock mechanism initialization succeeded.\n");

    // Init interrupt (^_^)
    init_exception();
    printk("> [INIT] Interrupt processing initialization succeeded.\n");

    // Init system call table (0_0)
    init_syscall();
    printk("> [INIT] System call initialized successfully.\n");

    // Init screen (QAQ)
    init_screen();
    printk("> [INIT] SCREEN initialization succeeded.\n");

    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's
    


    while (1) {
        bios_putstr("Please input task name:\n");

        char input_name[16] = "";
        int idx = 0;
        int ch;
        // 读取字符串直到回车
        while ((ch = bios_getchar()) != '\r' && idx < 31) {
            if (ch != -1) {
                input_name[idx++] = (char)ch;
                bios_putchar(ch); // 回显
            }
        }
        input_name[idx] = '\0';

        uint64_t entry_addr = load_task_img(input_name);
        bios_putstr("\n");
        if(entry_addr != 0){
            void (*entry)(void) = (void (*)(void))entry_addr;
            entry();
            bios_putstr("Task loaded. You can input again.\n\r");
        }
    }

    // [p1-task3] 交互式输入 task id 并运行对应用户程序
    // int tasknum = TASK_MAXNUM; // 或者根据实际任务数赋值
    // bios_putstr("Input task id to run (0 ~ ");
    // bios_putchar('0' + tasknum - 1); // 假设 tasknum 已知
    // bios_putstr("):\n\r");

    // int input_id = 0;
    // while (1) {
    //     bios_putstr("task id> ");
    //     input_id = 0;
    //     int ch;
    //     // 读取数字并回显
    //     while (1) {
    //         ch = bios_getchar();
    //         if (ch == '\n' || ch == '\r') break;
    //         if (ch >= '0' && ch <= '9') {
    //             bios_putchar(ch);
    //             input_id = input_id * 10 + (ch - '0');
    //         }
    //     }
    //     bios_putstr("\n\r");

    //     // 检查合法性
    //     if (input_id < 0 || input_id >= tasknum) {
    //         bios_putstr("Invalid task id!\n\r");
    //         continue;
    //     }

    //     // 加载并执行
    //     uint64_t entry_addr = load_task_img(input_id);
    //     bios_putstr("Loading task...\n\r");

    //     // 用函数指针跳转到用户程序入口
    //     void (*entry)(void) = (void (*)(void))entry_addr;
    //     entry();

    //     bios_putstr("Task loaded. You can input again.\n\r");
    // }
        
    // [p1-task2] 持续读取键盘输入并回显到屏幕
    // bios_putstr("Input from keyboard (echo):\n\r");
    // while (1)
    // {
    //     int ch = bios_getchar();   // 跳转表API读取键盘
    //     if (ch != -1)              // 只处理有效输入
    //         bios_putchar(ch);      // 跳转表API回显
    // }

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        // If you do non-preemptive scheduling, it's used to surrender control
        do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        // enable_preempt();
        // asm volatile("wfi");
    }

    return 0;
}
