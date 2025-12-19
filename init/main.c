#include <asm.h>
#include <asm/unistd.h>
#include <assert.h>
#include <common.h>
#include <csr.h>
#include <e1000.h>
#include <os/ioremap.h>
#include <os/irq.h>
#include <os/kernel.h>
#include <os/net.h>
#include <os/loader.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <os/task.h>
#include <os/time.h>
#include <pgtable.h>
#include <printk.h>
#include <screen.h>
#include <sys/syscall.h>
#include <type.h>
#include <plic.h>
extern void ret_from_exception();

static void cancel_identity_mapping(void) {
    // 获取内核页目录的虚拟地址
    PTE *pgdir = (PTE *)pa2kva(PGDIR_PA);
    // 清除低 1GB (0x00000000 - 0x40000000) 和次低 1GB (0x40000000 - 0x80000000)
    // boot.c 中建立的临时映射位于 0x50200000，属于第 1 项 (Sv39 L2)
    pgdir[0] = 0;
    pgdir[1] = 0;
    local_flush_tlb_all();
}

// Task info array
task_info_t tasks[TASK_MAXNUM];

// A simple barrier for core synchronization
volatile int boot_barrier = 0;
volatile int boot_cnt = 0;
uint64_t plic_addr = 0;
uint32_t nr_irqs = 0;

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

#define APP_INFO_ADDR_LOC 0xffffffc0502001f4

static void init_task_info() {
    // Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    int32_t *app_info_ptr = (int32_t *)APP_INFO_ADDR_LOC;
    int app_info_loc = app_info_ptr[0];
    int app_info_size = app_info_ptr[1];

    int start_sec = app_info_loc / SECTOR_SIZE;
    int offset = app_info_loc % SECTOR_SIZE; // 扇区内偏移
    int blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sec;
    bios_sd_read(TASK_INFO_MEM, blocknums, start_sec);
    uint64_t start_addr = pa2kva(TASK_INFO_MEM + offset);
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

    // 初始化所有寄存器为 0，避免脏数据
    for (int i = 0; i < 32; i++) {
        pt_regs->regs[i] = 0;
    }
    pt_regs->regs[1] = entry_point;      // ra
    pt_regs->regs[2] = user_stack;       // sp
    pt_regs->regs[4] = (uint64_t)pcb;    // tp
    pt_regs->regs[10] = (reg_t)argc;     // a0
    pt_regs->regs[11] = (reg_t)argv;     // a
    pt_regs->sstatus = SR_SPIE | SR_SUM; // SPIE set to enable user interrupt, SUM set to allow user memory access
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

void init_thread_stack(ptr_t kernel_stack, ptr_t user_stack, ptr_t entry_point, pcb_t *pcb, ptr_t arg) {
    regs_context_t *pt_regs =
        (regs_context_t *)(kernel_stack - sizeof(regs_context_t));

    // 初始化所有寄存器为 0，避免脏数据
    for (int i = 0; i < 32; i++) {
        pt_regs->regs[i] = 0;
    }
    pt_regs->regs[1] = entry_point;
    pt_regs->regs[2] = user_stack;
    pt_regs->regs[4] = (uint64_t)pcb;
    pt_regs->regs[10] = arg; // a0 = argument
    pt_regs->sstatus = SR_SPIE | SR_SUM;
    pt_regs->sepc = (uint64_t)entry_point;

    switchto_context_t *pt_switchto =
        (switchto_context_t *)((ptr_t)pt_regs - sizeof(switchto_context_t));
    pcb->kernel_sp = (reg_t)pt_switchto;
    pt_switchto->regs[0] = (reg_t)ret_from_exception;
    pt_switchto->regs[1] = (reg_t)pt_switchto;
}

static void init_pcb(void) {
    /* load needed tasks and init their corresponding PCB */
    // PCB for kernel
    pid0_pcb.status = TASK_RUNNING;
    pid0_pcb.pgdir = pa2kva(PGDIR_PA);
    pid0_pcb.tgid = 0;
    pid0_pcb.list.prev = NULL;
    pid0_pcb.list.next = NULL;
    pid0_pcb.mask = 0x3;
    pid0_pcb.status = TASK_RUNNING;
    s_pid0_pcb.pgdir = pa2kva(PGDIR_PA);
    s_pid0_pcb.tgid = -1;
    s_pid0_pcb.mask = 0x3;
    s_pid0_pcb.list.prev = NULL;
    s_pid0_pcb.list.next = NULL;
    init_pcb_stack(pid0_pcb.kernel_sp, pid0_pcb.user_sp, (uint64_t)ret_from_exception, &pid0_pcb, 0, NULL);
    init_pcb_stack(s_pid0_pcb.kernel_sp, s_pid0_pcb.user_sp, (uint64_t)ret_from_exception, &s_pid0_pcb, 0, NULL);
    for (int i = 0; i < NUM_MAX_TASK; i++)
        pcb[i].status = TASK_EXITED;
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
    syscall[SYSCALL_FREE_MEM] = (long (*)())get_free_memory;
    syscall[SYSCALL_PIPE_OPEN] = (long (*)())do_pipe_open;
    syscall[SYSCALL_PIPE_GIVE] = (long (*)())do_pipe_give_pages;
    syscall[SYSCALL_PIPE_TAKE] = (long (*)())do_pipe_take_pages;
    syscall[SYSCALL_TASKSET] = (long (*)())do_taskset;
    syscall[SYSCALL_THREAD_CREATE] = (long (*)())do_thread_create;
    syscall[SYSCALL_THREAD_EXIT] = (long (*)())do_thread_exit;
    syscall[SYSCALL_THREAD_JOIN] = (long (*)())do_thread_join;
    syscall[SYSCALL_NET_SEND] = (long (*)())do_net_send;
    syscall[SYSCALL_NET_RECV] = (long (*)())do_net_recv;
    syscall[SYSCALL_NET_RECV_STREAM] = (long (*)())do_net_recv_stream;
}
/************************************************************/

int main() {
    int hartid = get_current_cpu_id();
    if (hartid == 0) {

        init_jmptab(); // Init jump table provided by kernel and bios(ΦωΦ)

        smp_init(); // Init SMP lock mechanism
        lock_kernel();

        // Init task information (〃'▽'〃)
        init_task_info();
        // Read Flatten Device Tree (｡•ᴗ-)_
        time_base = bios_read_fdt(TIMEBASE);
        e1000 = (volatile uint8_t *)bios_read_fdt(ETHERNET_ADDR);
        plic_addr = bios_read_fdt(PLIC_ADDR);
        nr_irqs = (uint32_t)bios_read_fdt(NR_IRQS);
        printk("> [INIT] e1000: %lx, plic_addr: %lx, nr_irqs: %lx.\n", e1000, plic_addr, nr_irqs);

        // IOremap
        plic_addr = (uintptr_t)ioremap((uint64_t)plic_addr, 0x4000 * NORMAL_PAGE_SIZE);
        e1000 = (uint8_t *)ioremap((uint64_t)e1000, 8 * NORMAL_PAGE_SIZE);
        printk("> [INIT] IOremap initialization succeeded.\n");
        
        // Init network device
        e1000_init();
        printk("> [INIT] E1000 device initialized successfully.\n");

        init_pcb();        // Init Process Control Blocks |•'-'•) ✧
        init_locks();      // Init lock mechanism o(´^｀)o
        init_barriers();   // Init barrier mechanism (๑•̀ㅂ•́)و✧
        init_conditions(); // Init condition variable mechanism (•̀ω•́)✧
        init_semaphores(); // Init semaphore mechanism (ง •̀_•́)ง
        init_mbox();       // Init mailbox mechanism (づ｡◕‿‿◕｡)づ
        init_syscall();    // Init system call table (0_0)
        init_screen();     // Init screen (QAQ)
        init_swap();       // Init swap mechanism (◕‿◕✿)
        printk("> [INIT] All global initializations done.\n");
        printk("> [INIT] CPU time_base: %lu Hz\n", time_base);

        wakeup_other_hart();
        while (boot_cnt < NR_CPUS - 1);
        printk("> [INIT] All harts are up. Continuing boot...\n");
        cancel_identity_mapping();
    } else {
        /************************************************************/
        /*                  从核的等待与唤醒                        */
        /************************************************************/

        boot_cnt++;
        // 加锁打印，防止和 Hart 0 冲突
        lock_kernel();
        printk("> [INIT] Hart %d has woken up.\n", hartid);
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
        // Init plic
        plic_init(plic_addr, nr_irqs);
        printk("> [INIT] PLIC initialized successfully. addr = 0x%lx, nr_irqs=0x%x\n", plic_addr, nr_irqs);
        do_exec("shell", 0, NULL);
        printk("> [INIT] Shell task started on Hart 0.\n");
    }

    printk("> [INIT] Hart %d interrupt enabled.\n", hartid);
    unlock_kernel();

    enable_preempt();

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1) {
        asm volatile("wfi");
    }
    return 0;
}
