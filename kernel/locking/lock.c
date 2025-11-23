#include <atomic.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>

mutex_lock_t mlocks[LOCK_NUM];
int lock_used_num = 0;

void init_locks(void) {
    /* initialize mlocks */
    for (int i = 0; i < LOCK_NUM; i++) {
        mlocks[i].lock.status = UNLOCKED;
        mlocks[i].key = 0;
        init_list_head(&mlocks[i].block_queue);
    }
}

void spin_lock_init(spin_lock_t *lock) {
    /* initialize spin lock */
    lock->status = UNLOCKED;
}

int spin_lock_try_acquire(spin_lock_t *lock) {
    /* try to acquire spin lock */
    if (lock->status == UNLOCKED) {
        lock->status = LOCKED;
        return 1;
    }
    return 0;
}

void spin_lock_acquire(spin_lock_t *lock) {
    /* acquire spin lock */
    while (lock->status == LOCKED)
        ;
    lock->status = LOCKED;
}

void spin_lock_release(spin_lock_t *lock) {
    /* release spin lock */
    lock->status = UNLOCKED;
}

int do_mutex_lock_init(int key) {
    /* initialize mutex lock */
    for (int i = 0; i < lock_used_num; i++) {
        if (mlocks[i].key == key)
            return i;
    }
    if (lock_used_num >= LOCK_NUM)
        return 0;
    mlocks[lock_used_num].key = key;
    mlocks[lock_used_num].lock.status = UNLOCKED;
    init_list_head(&mlocks[lock_used_num].block_queue);
    return lock_used_num++;
}

void do_mutex_lock_acquire(int mlock_idx) {
    /* acquire mutex lock */
    if (spin_lock_try_acquire(&mlocks[mlock_idx].lock)) {
        mlocks[mlock_idx].pid = current_running->pid;
        return;
    }
    // 获取锁失败
    do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
    pcb_t *prior_running = current_running;
    current_running = get_pcb_from_node(seek_ready_node());
    current_running->status = TASK_RUNNING;
    switch_to(prior_running, current_running);
}

void do_mutex_lock_release(int mlock_idx) {
    /* release mutex lock */
    mutex_lock_t *lock = &mlocks[mlock_idx];
    list_node_t *head = &lock->block_queue, *p = head->next;
    // 阻塞队列为空，释放锁
    if (p == head) {
        spin_lock_release(&mlocks[mlock_idx].lock);
        lock->pid = -1;
    } else {
        mlocks[mlock_idx].pid = get_pcb_from_node(p)->pid;
        do_unblock(p);
    }
}

void release_all_lock(pid_t pid) {
    // Release all locks held by the process with the given pid
    for (int i = 0; i < LOCK_NUM; i++) {
        if (mlocks[i].pid == pid) {
            do_mutex_lock_release(i);
        }
    }
}