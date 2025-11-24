#include <asm.h>
#include <asm/unistd.h>
#include <assert.h>
#include <common.h>
#include <csr.h>
#include <os/irq.h>
#include <os/kernel.h>
#include <os/loader.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>
#include <sys/syscall.h>
#include <type.h>
extern void ret_from_exception();

// Task info array
task_info_t tasks[TASK_MAXNUM];

// A simple barrier for core synchronization
volatile int boot_barrier = 0;

static void init_jmptab(void) {
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;
    jmptab[CONSOLE_PUTSTR] = (volatile long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (volatile long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (volatile long (*)())port_read_ch;
    jmptab[SD_READ] = (volatile long (*)())sd_read;
    jmptab[SD_WRITE] = (volatile long (*)())sd_write;
    jmptab[QEMU_LOGGING] = (volatile long (*)())qemu_logging;
    jmptab[SET_TIMER] = (volatile long (*)())set_timer;
    jmptab[READ_FDT] = (volatile long (*)())read_fdt;
}

static void init_task_info(int app_info_loc, int app_info_size) {
    // Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    int start_sec, blocknums;
    start_sec = app_info_loc / SECTOR_SIZE;
    blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sec;
    int task_info_addr = TASK_INFO_MEM;
    bios_sd_read(task_info_addr, blocknums, start_sec);
    uint64_t start_addr = (TASK_INFO_MEM + app_info_loc - start_sec * SECTOR_SIZE);
    memcpy((uint8_t *)tasks, (uint8_t *)start_addr, app_info_size);
}

/************************************************************/
void init_pcb_stack(ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point, pcb_t *pcb, int argc, char *argv[]) {
    /* initialization of registers on kernel stack
     * HINT: sp, ra, sepc, sstatus
     * NOTE: To run the task in user mode, you should set corresponding bits
     *     of sstatus(SPP, SPIE, etc.).
     */
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));
    pt_regs->regs[1] = entry_point;   // ra
    pt_regs->regs[2] = user_stack;    // sp
    pt_regs->regs[4] = (uint64_t)pcb; // tp
    pt_regs->regs[10] = (reg_t)argc;  // a0
    pt_regs->regs[11] = (reg_t)argv;  // a
    pt_regs->sstatus = SR_SPIE;       // SPIE set to 1
    pt_regs->sepc = (uint64_t)entry_point;
    /* set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pcb->kernel_sp = (reg_t)pt_switchto;
    pt_switchto->regs[0] = (reg_t)ret_from_exception; // ra
    pt_switchto->regs[1] = (reg_t)pt_switchto;        // kernel_sp
}

static void init_pcb(void) {
    /* load needed tasks and init their corresponding PCB */
    // PCB for kernel
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.list.prev = NULL;
    pid0_pcb.list.next = NULL;
    pid0_pcb.mask = 0x3; // allow to run on core 0
    s_pid0_pcb.list.prev = NULL;
    s_pid0_pcb.list.next = NULL;
    s_pid0_pcb.mask = 0x3; // allow to run on core 1
    init_pcb_stack(pid0_pcb.kernel_sp, pid0_pcb.user_sp, (uint64_t)ret_from_exception, &pid0_pcb, 0, NULL);
    for (int i = 0; i < NUM_MAX_TASK; i++)
        pcb[i].status = TASK_EXITED;
    /* remember to initialize 'current_running' */
    current_running = &pid0_pcb;
}

static void init_syscall(void) {
    // initialize system call table.
    syscall[SYSCALL_EXEC] = (long (*)())do_exec;
    syscall[SYSCALL_EXIT] = (long (*)())do_exit;
    syscall[SYSCALL_SLEEP] = (long (*)())do_sleep;
    syscall[SYSCALL_KILL] = (long (*)())do_kill;
    syscall[SYSCALL_WAITPID] = (long (*)())do_waitpid;
    syscall[SYSCALL_PS] = (long (*)())do_process_show;
    syscall[SYSCALL_GETPID] = (long (*)())do_getpid;
    syscall[SYSCALL_YIELD] = (long (*)())do_scheduler;
    syscall[SYSCALL_LIST] = (long (*)())do_list;
    syscall[SYSCALL_WRITE] = (long (*)())screen_write;
    syscall[SYSCALL_READCH] = (long (*)())bios_getchar;
    syscall[SYSCALL_CURSOR] = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH] = (long (*)())screen_reflush;
    syscall[SYSCALL_CLEAR] = (long (*)())screen_clear;
    syscall[SYSCALL_WRITECH] = (long (*)())screen_write_ch;
    syscall[SYSCALL_GET_TIMEBASE] = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK] = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT] = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ] = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_BARR_INIT] = (long (*)())do_barrier_init;
    syscall[SYSCALL_BARR_WAIT] = (long (*)())do_barrier_wait;
    syscall[SYSCALL_BARR_DESTROY] = (long (*)())do_barrier_destroy;
    syscall[SYSCALL_COND_INIT] = (long (*)())do_condition_init;
    syscall[SYSCALL_COND_WAIT] = (long (*)())do_condition_wait;
    syscall[SYSCALL_COND_SIGNAL] = (long (*)())do_condition_signal;
    syscall[SYSCALL_COND_BROADCAST] = (long (*)())do_condition_broadcast;
    syscall[SYSCALL_COND_DESTROY] = (long (*)())do_condition_destroy;
    syscall[SYSCALL_SEMA_INIT] = (long (*)())do_semaphore_init;
    syscall[SYSCALL_SEMA_UP] = (long (*)())do_semaphore_up;
    syscall[SYSCALL_SEMA_DOWN] = (long (*)())do_semaphore_down;
    syscall[SYSCALL_SEMA_DESTROY] = (long (*)())do_semaphore_destroy;
    syscall[SYSCALL_MBOX_OPEN] = (long (*)())do_mbox_open;
    syscall[SYSCALL_MBOX_CLOSE] = (long (*)())do_mbox_close;
    syscall[SYSCALL_MBOX_SEND] = (long (*)())do_mbox_send;
    syscall[SYSCALL_MBOX_RECV] = (long (*)())do_mbox_recv;
    syscall[SYSCALL_TASKSET] = (long (*)())do_taskset;
}
/************************************************************/

int main(int hartid) {
    if (hartid == 0) {
        init_jmptab(); // Init jump table provided by kernel and bios(ΦωΦ)

        smp_init();  // Init SMP lock mechanism

        // Init task information (〃'▽'〃)
        uint32_t *app_info_ptr = (uint32_t *)APP_INFO_ADDR_LOC; // 需要定义 APP_INFO_ADDR_LOC
        int app_info_loc = app_info_ptr[0];
        int app_info_size = app_info_ptr[1];
        init_task_info(app_info_loc, app_info_size);
        
        // Read CPU frequency (｡•ᴗ-)_
        time_base = bios_read_fdt(TIMEBASE);
        printk("> [INIT] CPU time_base: %lu Hz\n", time_base);
        
        init_pcb(); // Init Process Control Blocks |•'-'•) ✧
        init_locks(); // Init lock mechanism o(´^｀)o      
        init_barriers();  // Init barrier mechanism o(´^｀)o      
        init_conditions(); // Init condition variable mechanism o(´^｀)o    
        init_semaphores();    // Init semaphore mechanism o(´^｀)o
        init_mbox();         // Init mailbox mechanism o(´^｀)o 
        init_syscall(); // Init system call table (0_0)
        init_screen(); // Init screen (QAQ)
        printk("> [INIT] All global initializations done.\n");

        wakeup_other_hart();
    } else {
        /************************************************************/
        /*                  从核的等待与唤醒                        */
        /************************************************************/

        // 从核在此自旋，等待主核完成全局初始化
        smp_wait_for_boot();
        // 加锁打印，防止和 Hart 0 冲突
        lock_kernel();
        printk("> [INIT] Hart %d has woken up.\n", hartid);
        unlock_kernel();
    }
    /****************************************************************/
    /*               每核初始化 (所有核都执行)                      */
    /****************************************************************/
    // Init interrupt (^_^)
    init_exception();
    // Setup timer interrupt and enable all interrupt globally
    // NOTE: 我们给从核的第一次中断加一点点延迟，避免所有核同时中断
    bios_set_timer(get_ticks() + TIMER_INTERVAL * (hartid + 1)); // 设置第一次定时器中断

    /****************************************************************/
    /*                   启动第一个进程并进入调度                     */
    /****************************************************************/

    if (hartid == 0) {
        do_exec("shell", 0, NULL);
        lock_kernel();
        printk("> [INIT] Shell task started on Hart 0.\n");
        unlock_kernel();
    }

    lock_kernel();
    printk("> [INIT] Hart %d interrupt enabled.\n", hartid);
    unlock_kernel();

    enable_preempt();

    /****************************************************************/
    /*                   启动第一个进程并进入调度                     */
    /****************************************************************/

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1) {
        asm volatile("wfi");
    }
    return 0;
}
