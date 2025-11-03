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
    jmptab[MOVE_CURSOR] = (volatile long (*)())screen_move_cursor;
    jmptab[WRITE] = (volatile long (*)())screen_write;
    jmptab[REFLUSH] = (volatile long (*)())screen_reflush;
    jmptab[PRINT] = (volatile long (*)())printk;
    jmptab[YIELD] = (volatile long (*)())do_scheduler;
    jmptab[MUTEX_INIT] = (volatile long (*)())do_mutex_lock_init;
    jmptab[MUTEX_ACQ] = (volatile long (*)())do_mutex_lock_acquire;
    jmptab[MUTEX_RELEASE] = (volatile long (*)())do_mutex_lock_release;

    // TODO: [p2-task1] (S-core) initialize system call table.
}

static void init_task_info(int app_info_loc, int app_info_size) {
    // Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    int start_sec, blocknums;
    start_sec = app_info_loc / SECTOR_SIZE;
    blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sec;
    int task_info_addr = TASK_INFO_MEM;
    bios_sd_read(task_info_addr, blocknums, start_sec);
    int start_addr = (TASK_INFO_MEM + app_info_loc - start_sec * SECTOR_SIZE);
    memcpy((uint8_t*)tasks, (uint8_t*)start_addr, app_info_size);
}

/************************************************************/
static void init_pcb_stack(ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point, pcb_t* pcb) {
    /* initialization of registers on kernel stack
     * HINT: sp, ra, sepc, sstatus
     * NOTE: To run the task in user mode, you should set corresponding bits
     *     of sstatus(SPP, SPIE, etc.).
     */
    regs_context_t* pt_regs =
        (regs_context_t*)(kernel_stack - sizeof(regs_context_t));
    pt_regs->regs[1] = (uint64_t)entry_point;           // ra
    pt_regs->regs[2] = user_stack; // sp
    pt_regs->regs[4] = (uint64_t)pcb;                             // tp
    pt_regs->sstatus = SR_SPIE;  // SPIE set to 1
    pt_regs->sepc = (uint64_t)entry_point;
    /* set sp to simulate just returning from switch_to
     * NOTE: you should prepare a stack, and push some values to
     * simulate a callee-saved context.
     */
    switchto_context_t* pt_switchto =
        (switchto_context_t*)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pcb->kernel_sp = (reg_t)pt_switchto;
    pt_switchto->regs[0] = (uint64_t)ret_from_exception; // ra
    pt_switchto->regs[1] = (reg_t)pt_switchto;           // kernel_sp
}

static void init_pcb(void) {
    /* load needed tasks and init their corresponding PCB */
    // PCB for kernel
    uint64_t entry[NUM_MAX_TASK + 1]; /* entry of all tasks */
    char needed_tasks[][16] = {
        "print1", "print2", "lock1", "lock2", "sleep", "timer", "fly",
        "fly1", "fly2", "fly3", "fly4", "fly5"
    };
    uint64_t entry_addr;
    int tasknum = 0;
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.list.prev = NULL;
    pid0_pcb.list.next = NULL;
    init_pcb_stack(pid0_pcb.kernel_sp, pid0_pcb.user_sp,
        (uint64_t)ret_from_exception, &pid0_pcb);
    // load task by name;
    for (int i = 7; i < 12; i++) {
        entry_addr = load_task_img(needed_tasks[i]);
        // create a PCB
        if (entry_addr != 0) {
            pcb[tasknum].kernel_sp =
                (reg_t)(allocKernelPage(1) + PAGE_SIZE); // 分配一页
            pcb[tasknum].user_sp = (reg_t)(allocUserPage(1) + PAGE_SIZE);
            pcb[tasknum].pid = tasknum + 1; // pid 0 is for kernel
            pcb[tasknum].status = TASK_READY;
            pcb[tasknum].cursor_x = 0;
            pcb[tasknum].cursor_y = 0;
            pcb[tasknum].wakeup_time = 0;
            pcb[tasknum].workload = 0;
            // pcb[tasknum].time_slice = 10;
            // pcb[tasknum].time_slice_remain = 10;
            init_pcb_stack(pcb[tasknum].kernel_sp, pcb[tasknum].user_sp, entry_addr,
                &pcb[tasknum]);
            // add to ready queue
            add_node_to_q(&pcb[tasknum].list, &ready_queue);

            if (++tasknum > NUM_MAX_TASK) // total tasks should be less than the threshold
                break;
        }
    }

    /* remember to initialize 'current_running' */
    current_running = &pid0_pcb;
}

static void init_syscall(void) {
    // initialize system call table.
    syscall[SYSCALL_SLEEP] = (long (*)())do_sleep;
    syscall[SYSCALL_YIELD] = (long (*)())do_scheduler;
    syscall[SYSCALL_WRITE] = (long (*)())screen_write;
    syscall[SYSCALL_CURSOR] = (long (*)())screen_move_cursor;
    syscall[SYSCALL_REFLUSH] = (long (*)())screen_reflush;
    syscall[SYSCALL_GET_TIMEBASE] = (long (*)())get_time_base;
    syscall[SYSCALL_GET_TICK] = (long (*)())get_ticks;
    syscall[SYSCALL_LOCK_INIT] = (long (*)())do_mutex_lock_init;
    syscall[SYSCALL_LOCK_ACQ] = (long (*)())do_mutex_lock_acquire;
    syscall[SYSCALL_LOCK_RELEASE] = (long (*)())do_mutex_lock_release;
    syscall[SYSCALL_SET_SCHE_WORKLOAD] = (long (*)())do_set_sche_workload;
}
/************************************************************/

int main(int app_info_loc, int app_info_size) {
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
    printk("> [INIT] CPU time_base: %lu Hz\n", time_base); 
    // TODO: [p2-task4] Setup timer interrupt and enable all interrupt globally
    // NOTE: The function of sstatus.sie is different from sie's
    bios_set_timer(get_ticks() + TIMER_INTERVAL); // 设置第一次定时器中断
    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    enable_preempt();
    while (1) {
        // If you do non-preemptive scheduling, it's used to surrender control
        // do_scheduler();

        // If you do preemptive scheduling, they're used to enable CSR_SIE and wfi
        asm volatile("wfi");
    }

    return 0;
}
