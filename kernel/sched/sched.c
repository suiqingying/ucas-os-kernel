#include <assert.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/mm.h>
#include <os/sched.h>
#include <os/time.h>
#include <printk.h>
#include <screen.h>

#define LENGTH 60

pcb_t pcb[NUM_MAX_TASK];
const ptr_t pid0_stack = INIT_KERNEL_STACK + PAGE_SIZE;
pcb_t pid0_pcb = {
    .pid = 0, .kernel_sp = (ptr_t)pid0_stack, .user_sp = (ptr_t)pid0_stack };

LIST_HEAD(ready_queue);
LIST_HEAD(sleep_queue);

/* global process id */
pid_t process_id = 1;

void do_scheduler(void) {
    update_time_slices();
    // Check sleep queue to wake up PCBs
    check_sleeping();
    /************************************************************/
    /* Do not touch this comment. Reserved for future projects. */
    /************************************************************/

    // Modify the current_running pointer.
    pcb_t* prior_running;
    prior_running = current_running;

    if (current_running->pid != 0) {
        // add to the ready queue
        if (current_running->status == TASK_RUNNING) {
            current_running->status = TASK_READY;
            add_node_to_q(&current_running->list, &ready_queue);
        }
        // else if (current_running->status == TASK_BLOCKED)
        //     add_node_to_q(&current_running->list, &sleep_queue);
    }
    list_node_t* tmp = seek_ready_node();

    current_running = get_pcb_from_node(tmp);
    current_running->status = TASK_RUNNING;

    printl("pid[%d]:is going to running\n", current_running->pid);

    current_running->time_slice_remain = current_running->time_slice;
    // switch_to current_running
    switch_to(prior_running, current_running);
    printl("[%d] switch_to success!!!\n", current_running->pid);
    return;
}

void do_sleep(uint32_t sleep_time) {
    // sleep(seconds)
    // NOTE: you can assume: 1 second = 1 `timebase` ticks
    // 1. block the current_running
    do_block(&current_running->list, &sleep_queue); // 添加这一行
    // do_block(&current_running->list, &sleep_queue);
    // 2. set the wake up time for the blocked task
    current_running->wakeup_time = get_timer() + sleep_time;
    // 3. reschedule because the current_running is blocked.
    do_scheduler();
}

void do_block(list_node_t* pcb_node, list_head* queue) {
    // block the pcb task into the block queue
    pcb_t* pcb = get_pcb_from_node(pcb_node);
    pcb->status = TASK_BLOCKED;
    add_node_to_q(pcb_node, queue);
}

void do_unblock(list_node_t* pcb_node) {
    // unblock the `pcb` from the block queue
    delete_node_from_q(pcb_node);
    pcb_t* pcb = get_pcb_from_node(pcb_node);
    pcb->status = TASK_READY;
    add_node_to_q(pcb_node, &ready_queue);
}

list_node_t* seek_ready_node() {
    list_node_t* p = ready_queue.next;
    delete_node_from_q(p);
    return p;
}

void init_list_head(list_head* list) {
    list->prev = list;
    list->next = list;
}

void add_node_to_q(list_node_t* node, list_head* head) {
    list_node_t* p = head->prev; // tail ptr
    p->next = node;
    node->prev = p;
    node->next = head;
    head->prev = node; // update tail ptr
}

void delete_node_from_q(list_node_t* node) {
    list_node_t* p, * q;
    p = node->prev;
    q = node->next;
    p->next = q;
    q->prev = p;
    node->next = node->prev = NULL; // delete the node completely
}

pcb_t* get_pcb_from_node(list_node_t* node) {
    for (int i = 0; i < NUM_MAX_TASK; i++) {
        if (node == &pcb[i].list)
            return &pcb[i];
    }
    return &pid0_pcb; // fail to find the task, return to kernel
}

void do_set_sche_workload(int workload) {
    current_running->workload = workload;
    pcb_t* task = current_running;
    if (workload < task->last_workload)
        task->lap_count++;
    uint64_t true_workload = (task->lap_count * LENGTH) + LENGTH - workload;
    task->workload = true_workload;
    task->last_workload = workload;
}

void update_time_slices(void) {
    // for (int i = 0; i < NUM_MAX_TASK; i++) {
    //     if (pcb[i].status == TASK_READY || pcb[i].status == TASK_RUNNING) {
    //         pcb[i].time_slice = 5 + (pcb[i].workload / 10) + 1;
    //     }
    // }
    list_node_t* node = ready_queue.next;
    uint64_t max_workload = -1;
    // 1. 找出当前所有就绪任务中最小的 workload（最领先的飞机）
    //    由于 do_set_sche_workload 的翻译，这里的 pcb->workload 已经是“真实”进度
    while (node != &ready_queue) {
        pcb_t* pcb = get_pcb_from_node(node);
        if (pcb->status == TASK_READY && (max_workload == -1 || pcb->workload > max_workload)) {
            max_workload = pcb->workload;
        }
        node = node->next;
    }

    // 如果没有就绪任务，则不更新
    if (max_workload == -1) return;

    // 2. 根据与最小 workload 的差距（lag），重新计算每个任务的时间片配额
    node = ready_queue.next;
    while (node != &ready_queue) {
        pcb_t* pcb = get_pcb_from_node(node);
        if (pcb->status == TASK_READY) {
            int64_t lag = max_workload - pcb->workload;

            // 基础时间片为5，并根据落后程度增加
            // 加1是为了防止 lag/10 为0
            pcb->time_slice = 5 + (lag / 10) + 1;
        }
        node = node->next;
    }
}