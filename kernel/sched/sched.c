#include <assert.h>
#include <os/list.h>
#include <os/loader.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>
#define LENGTH 60
pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0, .kernel_sp = (ptr_t)pid0_stack, .user_sp = (ptr_t)pid0_stack};

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
    pcb_t *prior_running;
    prior_running = current_running;

    if (current_running->pid != 0) {
        // add to the ready queue
        if (current_running->status == TASK_RUNNING) {
            current_running->status = TASK_READY;
            add_node_to_q(&current_running->list, &ready_queue);
        }
    }
    list_node_t *tmp = seek_ready_node();

    current_running = get_pcb_from_node(tmp);
    current_running->status = TASK_RUNNING;

    printl("pid[%d]:is going to running\n", current_running->pid);

    // switch_to current_running
    switch_to(prior_running, current_running);
    printl("[%d] switch_to success!!!\n", current_running->pid);
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
    list_node_t *p = ready_queue.next;
    delete_node_from_q(p);
    return p;
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
    return &pid0_pcb; // fail to find the task, return to kernel
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

void do_process_show() {
    static char *stat_str[3] = {"BLOCKED", "RUNNING", "READY"};
    screen_write("[Process table]:\n");
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (pcb[i].status != TASK_EXITED)
            printk("[%d] PID : %d  STATUS : %s \n", i, pcb[i].pid, stat_str[pcb[i].status]);
    }
}

pid_t do_getpid() { return current_running->pid; }