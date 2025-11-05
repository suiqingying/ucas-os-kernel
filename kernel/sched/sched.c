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

void do_set_sche_workload(uint64_t remain_length) {
    if (remain_length > current_running->remain_length) {
        current_running->lap_count++;
    }
    current_running->remain_length = remain_length;
}
void do_set_checkpoint(uint64_t checkpoint) {
    current_running->checkpoint = checkpoint;
}

void update_time_slices(void) {
    const int D = 60; // Total distance of the track
    const int K = 30; // A scaling factor for the lag

    // Step 1: For each ready task, calculate its normalized progress
    list_node_t* node = ready_queue.next;
    while (node != &ready_queue) {
        pcb_t* pcb = get_pcb_from_node(node);
        if (pcb->pid != 0) { // Only schedule user tasks
            uint64_t lap_progress = (uint64_t)pcb->lap_count * 2000;
            uint64_t distance_in_lap = D - pcb->remain_length;
            uint64_t checkpoint = pcb->checkpoint;

            if (checkpoint > 0 && checkpoint < D) {
                if (distance_in_lap < checkpoint) {
                    // Pre-checkpoint phase: 0-1000 points for this lap
                    pcb->normalized_progress = lap_progress + (distance_in_lap * 1000) / checkpoint;
                }
                else {
                    // Post-checkpoint phase: 1000-2000 points for this lap
                    pcb->normalized_progress = lap_progress + 1000 + ((distance_in_lap - checkpoint) * 1000) / (D - checkpoint);
                }
            }
        }
        node = node->next;
    }

    // Step 2: Find the max progress (the leader)
    uint64_t max_progress = 0;
    node = ready_queue.next;
    while (node != &ready_queue) {
        pcb_t* pcb = get_pcb_from_node(node);
        if (pcb->pid != 0) {
            if (pcb->normalized_progress > max_progress) {
                max_progress = pcb->normalized_progress;
            }
        }
        node = node->next;
    }

    // Step 3: Assign time slices based on lag from the leader
    node = ready_queue.next;
    while (node != &ready_queue) {
        pcb_t* pcb = get_pcb_from_node(node);
        if (pcb->pid != 0) {
            uint64_t lag = max_progress - pcb->normalized_progress;
            pcb->time_slice = (lag / K) + 1;
        }
        node = node->next;
    }
}