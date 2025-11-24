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
    .pid = 0, .kernel_sp = (ptr_t)pid0_stack, .user_sp = (ptr_t)pid0_stack, .status = TASK_RUNNING};
const ptr_t s_pid0_stack = INIT_KERNEL_STACK + 2 * PAGE_SIZE; // S_KERNEL_STACK_TOP from head.S
pcb_t s_pid0_pcb = {
    .pid = -1, .kernel_sp = (ptr_t)s_pid0_stack, .user_sp = (ptr_t)s_pid0_stack, .status = TASK_RUNNING};
LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void) {
    // Check sleep queue to wake up PCBs
    check_sleeping();
    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
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
    switch_to(prior_running, current_running); // switch_to current_running
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
    if (current_running->pid != p->pid)
        delete_node_from_q(&(p->list));
    free_block_list(&(p->wait_list));
    release_all_lock(p->pid);
}

int task_num = 0;
pid_t do_exec(char *name, int argc, char *argv[]) {
    // printk("argc: %d\n", argc);
    char **argv_ptr;
    int index = search_free_pcb();
    if (index == -1) return 0;
    uint64_t entry_point;
    entry_point = load_task_img(name);
    if (entry_point == 0)
        return 0;
    else {
        pcb[index].kernel_sp = (reg_t)(allocKernelPage(1) + PAGE_SIZE);
        pcb[index].user_sp = (reg_t)(allocUserPage(1) + PAGE_SIZE);
        pcb[index].pid = task_num + 1; // pid 0 is for kernel
        pcb[index].status = TASK_READY;
        pcb[index].cursor_x = 0;
        pcb[index].cursor_y = 0;
        pcb[index].wait_list.prev = pcb[index].wait_list.next = &pcb[index].wait_list;
        pcb[index].list.prev = pcb[index].list.next = NULL;
        pcb[index].mask = current_running->mask;
        // 参数搬到用户栈
        uint64_t user_sp = pcb[index].user_sp;
        user_sp -= sizeof(char *) * argc;
        argv_ptr = (char **)user_sp;

        for (int i = argc - 1; i >= 0; i--) {
            int len = strlen(argv[i]) + 1; // 要拷贝'\0'
            user_sp -= len;
            argv_ptr[i] = (char *)user_sp;
            strcpy((char *)user_sp, argv[i]);
        }
        pcb[index].user_sp = (reg_t)ROUNDDOWN(user_sp, 128); // 栈指针128字节对齐
        // 初始化栈，改变入口地址，存储参数
        init_pcb_stack(pcb[index].kernel_sp, pcb[index].user_sp, entry_point, &pcb[index], argc, argv_ptr);
        // 加入ready队列
        add_node_to_q(&pcb[index].list, &ready_queue);
        // 进程数加一
        task_num++;
        // printk("%d\n",task_num);
    }
    return pcb[index].pid; // 返回pid值
}

void do_exit() {
    current_running->status = TASK_EXITED;
    pcb_release(current_running);
    do_scheduler();
}

int do_kill(pid_t pid) {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].status != TASK_EXITED && pcb[i].pid == pid) {
            pcb[i].status = TASK_EXITED;
            pcb_release(&pcb[i]);
            return 1;
        }
    }
    return 0;
}

int do_waitpid(pid_t pid) {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].pid == pid) {
            if (pcb[i].status != TASK_EXITED) {
                do_block(&(current_running->list), &(pcb[i].wait_list));
                do_scheduler();
                return pid;
            }
        }
    }
    return 0;
}

/* waitpid 的设计应该是当前进程等待指定的进程，而不是所有进程都等待该进程 */

void do_process_show() {
    static char *stat_str[3] = {"BLOCKED", "RUNNING", "READY"};
    printk("[Process Table]:\n");
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].status != TASK_EXITED) {
            // 打印 Mask
            printk("[%d] PID : %d  STATUS : %s  MASK : 0x%x", 
                   i, pcb[i].pid, stat_str[pcb[i].status], pcb[i].mask);
            
            // 如果是 RUNNING，打印当前所在的核
            // 注意：多核环境下判断哪个核跑哪个进程比较复杂，
            // 这里我们简单判断一下是否是当前核正在跑的
            if (pcb[i].status == TASK_RUNNING) {
                if (pcb[i].pid == current_running->pid) {
                     printk(" [ON CORE %d]", get_current_cpu_id());
                } else {
                     // 如果它在 Running 但不是当前核跑的，那肯定是在另一个核
                     printk(" [ON CORE %d]", 1 - get_current_cpu_id());
                }
            }
            printk("\n");
        }
    }
}

pid_t do_getpid() { return current_running->pid; }

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