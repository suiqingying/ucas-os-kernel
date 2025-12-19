#include <assert.h>
#include <os/list.h>
#include <os/loader.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>
#define LENGTH 60
pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0, .tgid = 0, .kernel_sp = (ptr_t)pid0_stack, .user_sp = (ptr_t)pid0_stack, .kernel_stack_base = 0, .user_stack_base = 0, .status = TASK_RUNNING, .thread_ret = NULL, .allocated_pages = 0};
const ptr_t s_pid0_stack = INIT_KERNEL_STACK + 2 * PAGE_SIZE; // S_KERNEL_STACK_TOP from head.S
pcb_t s_pid0_pcb = {
    .pid = -1, .tgid = -1, .kernel_sp = (ptr_t)s_pid0_stack, .user_sp = (ptr_t)s_pid0_stack, .kernel_stack_base = 0, .user_stack_base = 0, .status = TASK_RUNNING, .thread_ret = NULL, .allocated_pages = 0};
LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void) {
    // Check sleep queue to wake up PCBs
    check_sleeping();

    /************************************************************/

    // Modify the current_running pointer.
    pcb_t *prior_running = current_running;

    if (current_running->pid != 0 && current_running->pid != -1) {
        // add to the ready queue
        if (current_running->status == TASK_RUNNING) {
            current_running->status = TASK_READY;
            add_node_to_q(&current_running->list, &ready_queue);
        }
    }

    list_node_t *next_node = seek_ready_node();
    current_running = get_pcb_from_node(next_node);
    current_running->status = TASK_RUNNING;

    // [重点] 切换用户页表
    // 注意：必须使用物理地址写入 satp
    uintptr_t pgdir_pa = kva2pa(current_running->pgdir);
    // 重新设置 satp 寄存器（set_satp内部已经有sfence.vma）
    set_satp(SATP_MODE_SV39, current_running->pid, pgdir_pa >> NORMAL_PAGE_SHIFT);
    switch_to(prior_running, current_running);
    return;
}

void do_sleep(uint32_t sleep_time) {
    // sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    do_block(&current_running->list, &sleep_queue);
    // do_block(&current_running->list, &sleep_queue);
    // 2. set the wake up time for the blocked task
    current_running->wakeup_time = get_timer() + sleep_time;
    // 3. reschedule because the current_running is blocked.
    do_scheduler();
}

void do_block(list_node_t *pcb_node, list_head *queue) {
    // block the pcb task into the block queue
    pcb_t *pcb = get_pcb_from_node(pcb_node);
    pcb->status = TASK_BLOCKED;
    add_node_to_q(pcb_node, queue);
}

void do_unblock(list_node_t *pcb_node) {
    // unblock the `pcb` from the block queue
    delete_node_from_q(pcb_node);
    pcb_t *pcb = get_pcb_from_node(pcb_node);
    pcb->status = TASK_READY;
    add_node_to_q(pcb_node, &ready_queue);
}

list_node_t *seek_ready_node() {
    /*list_node_t *p = ready_queue.next;
    if (p == &ready_queue) return NULL;
    delete_node_from_q(p);
    return p;*/
    int current_cpu = get_current_cpu_id();
    list_node_t *pos = ready_queue.next;

    // 遍历就绪队列
    while (pos != &ready_queue) {
        pcb_t *candidate_pcb = get_pcb_from_node(pos);

        // 检查 mask 的第 current_cpu 位是否为 1
        if (candidate_pcb->status == TASK_READY &&
            (candidate_pcb->mask & (1 << current_cpu))) {

            // 找到了符合当前核运行条件的任务, 将其从队列中移除并返回
            delete_node_from_q(pos);
            return pos;
        }

        // 如果不符合条件，继续找下一个
        pos = pos->next;
    }

    // 遍历了一圈都没找到适合当前核的任务，返回 NULL
    return NULL;
}

void init_list_head(list_head *list) {
    list->prev = list;
    list->next = list;
}

void add_node_to_q(list_node_t *node, list_head *head) {
    list_node_t *p = head->prev; // tail ptr
    p->next = node;
    node->prev = p;
    node->next = head;
    head->prev = node; // update tail ptr
}

void delete_node_from_q(list_node_t *node) {
    assert(node != NULL);
    assert(node->prev != NULL);
    assert(node->next != NULL);
    list_node_t *p, *q;
    p = node->prev;
    q = node->next;
    p->next = q;
    q->prev = p;
    node->next = node->prev = NULL; // delete the node completely
}

pcb_t *get_pcb_from_node(list_node_t *node) {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (node == &pcb[i].list)
            return &pcb[i];
    }
    if (get_current_cpu_id() == 0)
        return &pid0_pcb;
    else
        return &s_pid0_pcb;
}

int search_free_pcb() {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].status == TASK_EXITED) {
            return i;
        }
    }
    return -1; // No free PCB found
}

void free_block_list(list_node_t *head) {
    list_node_t *current = head->next;
    list_node_t *next_node;

    while (current != head) {
        next_node = current->next;
        do_unblock(current);
        current = next_node;
    }
}

void pcb_release(pcb_t *p) {

    free_block_list(&(p->wait_list));
    release_all_lock(p->pid);
    if (p->kernel_stack_base) {
        freePage(p->kernel_stack_base);
        p->kernel_stack_base = 0;
    }
    if (p->pgdir) {
        free_pgtable_pages(p->pgdir);
        p->pgdir = 0;
    }
}

int task_num = 0;
pid_t do_exec(char *name, int argc, char *argv[]) {
    // 1. 查找空闲 PCB
    int index = search_free_pcb();
    if (index == -1) {
        printk("Error: PCB full!\n");
        return 0;
    }
    pcb_t *new_pcb = &pcb[index];

    // 2. 分配页目录 (PGD)
    // allocPage 返回 KVA，方便内核直接访问
    uintptr_t pgdir = allocPage(1);
    clear_pgdir(pgdir);

    // 3. 复制内核映射
    // share_pgtable 需要物理地址，所以用 kva2pa 转换
    share_pgtable(kva2pa(pgdir), PGDIR_PA);

    // 4. 加载任务
    // 传入 pgdir (KVA)，load_task_img 会自动处理页分配和映射
    uint64_t entry_point = load_task_img(name, pgdir);
    if (entry_point == 0) {
        // 资源回收逻辑省略（实验中可忽略）
        return 0;
    }

    // 5. 分配内核栈
    uintptr_t kernel_stack_base = allocPage(1);
    uintptr_t kernel_stack = kernel_stack_base + PAGE_SIZE;

    // 6. 初始化用户栈并拷贝参数 (Args Copying)
    // 栈底: 0xf00010000
    uintptr_t user_stack_base = USER_STACK_ADDR;

    // 6.1 分配栈顶物理页，并获取其内核虚拟地址 (KVA)
    // 注意：USER_STACK_ADDR 是栈底，实际有效内存是从 USER_STACK_ADDR - PAGE_SIZE 开始
    uintptr_t stack_page_kva = alloc_page_helper(user_stack_base - PAGE_SIZE, pgdir);

    // 6.2 准备栈操作游标
    // k_sp: 内核视角的栈指针 (用于写入数据)
    // u_sp: 用户视角的栈指针 (用于计算指针地址)
    uintptr_t k_sp = stack_page_kva + PAGE_SIZE;
    uintptr_t u_sp = user_stack_base;

    // 暂存参数字符串在用户空间的地址
    uintptr_t user_argv_addrs[32]; // 假设参数不超过32个

    // 6.3 拷贝字符串 (从高地址向低地址压栈)
    for (int i = 0; i < argc; i++) {
        int len = strlen(argv[i]) + 1;
        k_sp -= len;
        u_sp -= len;
        strcpy((char *)k_sp, argv[i]);
        user_argv_addrs[i] = u_sp; // 记录该字符串在用户空间的虚拟地址
    }

    // 6.4 拷贝 argv 指针数组
    // 数组中存放的是字符串在用户空间的地址 (user_argv_addrs)
    k_sp -= sizeof(uintptr_t) * argc;
    u_sp -= sizeof(uintptr_t) * argc;

    // 6.5 栈指针对齐 (RISC-V 要求 128-bit/16-byte 对齐最佳，至少 8-byte)
    uintptr_t remain = u_sp % 16;
    if (remain != 0) {
        k_sp -= remain;
        u_sp -= remain;
    }

    uintptr_t *k_argv_ptr = (uintptr_t *)k_sp;
    for (int i = 0; i < argc; i++) {
        k_argv_ptr[i] = user_argv_addrs[i];
    }

    // 此时 u_sp 指向 argv 数组的起始位置，这正是 main 函数的第二个参数
    uintptr_t final_argv = u_sp;
    uintptr_t final_sp = u_sp;

    // 7. 初始化 PCB
    new_pcb->pid = ++task_num;
    new_pcb->tgid = new_pcb->pid;
    new_pcb->status = TASK_READY;
    new_pcb->mask = current_running->mask;
    new_pcb->pgdir = pgdir; // 存储 KVA，方便 set_satp 时转换
    new_pcb->kernel_sp = kernel_stack;
    new_pcb->kernel_stack_base = kernel_stack_base;
    new_pcb->user_sp = final_sp;
    new_pcb->cursor_x = 0;
    new_pcb->cursor_y = 0;
    new_pcb->mask = current_running->mask;
    new_pcb->thread_ret = NULL;
    // 初始化管道数组
    new_pcb->num_open_pipes = 0;
    for (int i = 0; i < MAX_PROCESS_PIPES; i++) {
        new_pcb->open_pipes[i] = -1;
    }
    // 初始化内存页面计数
    new_pcb->allocated_pages = 0;
    // 初始化链表
    init_list_head(&new_pcb->list);
    new_pcb->wait_list.prev = new_pcb->wait_list.next = &new_pcb->wait_list;

    // 8. 初始化寄存器上下文
    // 注意：这里传入的 argv 必须是用户空间的地址 (final_argv)
    init_pcb_stack(new_pcb->kernel_sp, new_pcb->user_sp, entry_point, new_pcb, argc, (char **)final_argv);

    // 9. 加入就绪队列
    add_node_to_q(&new_pcb->list, &ready_queue);
    return new_pcb->pid;
}

pid_t do_thread_create(ptr_t entry_point, ptr_t arg) {
    if (entry_point == 0)
        return -1;

    int index = search_free_pcb();
    if (index == -1)
        return -1;

    pcb_t *new_pcb = &pcb[index];
    // A thread shares the address space of its process
    new_pcb->pgdir = current_running->pgdir;

    // Allocate a new kernel stack for the new thread
    ptr_t kernel_stack_base = allocPage(1);
    pcb[index].kernel_sp = kernel_stack_base + PAGE_SIZE;
    pcb[index].kernel_stack_base = kernel_stack_base;

    // Allocate a new user stack for the new thread
    uintptr_t thread_stack_base = USER_STACK_ADDR - (index * PAGE_SIZE);
    alloc_page_helper(thread_stack_base - PAGE_SIZE, new_pcb->pgdir);
    new_pcb->user_sp = thread_stack_base; // 栈向下增长，初始指向高地址
    pcb[index].pid = ++task_num;
    new_pcb->tgid = current_running->tgid;
    new_pcb->status = TASK_READY;
    new_pcb->cursor_x = current_running->cursor_x;
    new_pcb->cursor_y = current_running->cursor_y;
    new_pcb->wait_list.prev = new_pcb->wait_list.next = &new_pcb->wait_list;
    init_list_head(&new_pcb->list);
    new_pcb->mask = current_running->mask;
    new_pcb->thread_ret = NULL;
    // 初始化管道数组
    new_pcb->num_open_pipes = 0;
    for (int i = 0; i < MAX_PROCESS_PIPES; i++) {
        new_pcb->open_pipes[i] = -1;
    }
    // 初始化内存页面计数
    new_pcb->allocated_pages = 0;

    init_thread_stack(new_pcb->kernel_sp, new_pcb->user_sp, entry_point, new_pcb, arg);
    add_node_to_q(&new_pcb->list, &ready_queue);
    return new_pcb->pid;
}

void do_thread_exit(ptr_t retval) {
    current_running->thread_ret = (void *)retval;
    current_running->status = TASK_EXITED;
    pcb_release(current_running);
    do_scheduler();
}

int do_thread_join(pid_t tid, ptr_t retval_ptr) {
    if (tid == current_running->pid)
        return -1;

    pcb_t *target = NULL;
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid == tid) {
            target = &pcb[i];
            break;
        }
    }

    if (target == NULL || target->tgid != current_running->tgid)
        return -1;

    if (target->status != TASK_EXITED) {
        do_block(&current_running->list, &target->wait_list);
        do_scheduler();
    }

    if (retval_ptr != 0) {
        *(ptr_t *)retval_ptr = (ptr_t)target->thread_ret;
    }

    return 0;
}

void do_exit(void) {
    current_running->status = TASK_EXITED; // 1. 设置状态

    // Close all pipes this process opened
    for (int i = 0; i < current_running->num_open_pipes; i++) {
        int pipe_idx = current_running->open_pipes[i];
        if (pipe_idx >= 0) {
            do_pipe_close(pipe_idx);
            current_running->open_pipes[i] = -1;
        }
    }
    current_running->num_open_pipes = 0;

    pcb_release(current_running); // 2. 释放资源 (页表、物理页、内核栈、锁等)
    do_scheduler(); // 3. 调度
}

int do_kill(pid_t pid) {
    pcb_t *target = NULL;

    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid == pid && pcb[i].status != TASK_EXITED) {
            target = &pcb[i];
            break;
        }
    }

    if (target == NULL || pid == 0) return 0; // 禁止杀死内核

    if (target->status == TASK_READY || target->status == TASK_BLOCKED) {
        delete_node_from_q(&target->list);
    }

    target->status = TASK_EXITED;

    // 统一资源释放 (锁、队列等)
    pcb_release(target);

    // 如果杀死了自己，需要调度
    if (target == current_running) {
        do_scheduler();
    }

    return 1;
}

int do_waitpid(pid_t pid) {
    pcb_t *target = NULL;
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid == pid) {
            target = &pcb[i];
            break;
        }
    }

    if (target == NULL) {
        return 0; // 子进程不存在
    }

    // 如果目标进程还未退出，则阻塞等待
    if (target->status != TASK_EXITED) {
        do_block(&(current_running->list), &(target->wait_list));
        do_scheduler();
    }

    // 此时目标进程已退出，可以进行资源回收（如果实现了僵尸状态）
    // 例如：free_pcb(target);
    return pid; // 返回已退出子进程的PID
}

/* waitpid 的设计应该是当前进程等待指定的进程，而不是所有进程都等待该进程 */

void do_process_show() {
    static char *stat_str[3] = {"BLOCKED", "RUNNING", "READY"};
    printu("[Process Table]:\n");
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].status != TASK_EXITED) {
            const char *role = (pcb[i].tgid == pcb[i].pid) ? "PROC" : "THRD";
            int proc_id = (pcb[i].tgid >= 0) ? pcb[i].tgid : pcb[i].pid;
            printu("[%d] %s PROC:%d TID:%d STATUS:%s MASK:0x%x",
                   i, role, proc_id, pcb[i].pid, stat_str[pcb[i].status], pcb[i].mask);

            if (pcb[i].status == TASK_RUNNING) {
                if (pcb[i].pid == current_running->pid) {
                    printu(" [ON CORE %d]", get_current_cpu_id());
                } else {
                    printu(" [ON CORE %d]", 1 - get_current_cpu_id());
                }
            }
            printu("\n");
        }
    }
}

pid_t do_getpid() { return current_running->tgid; }

void do_taskset(pid_t pid, int mask) {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid == pid && pcb[i].status != TASK_EXITED) {
            pcb[i].mask = mask;
            /*  这里我们只修改了 mask。
                如果该任务正在错误的核心上运行，我们暂时不强制迁移它。
                等它下一次调度时，do_scheduler 会自动根据 mask 决定它去哪里。
            */
            return;
        }
    }
}
