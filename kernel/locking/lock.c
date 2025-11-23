#include <atomic.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>
#include <printk.h>
mutex_lock_t mlocks[LOCK_NUM];
int lock_used_num = 0;

barrier_t barrs[BARRIER_NUM];
condition_t conds[CONDITION_NUM];
mailbox_t mbox[MBOX_NUM];
semaphore_t semaphores[SEMAPHORE_NUM];

void init_locks(void) {
    /* initialize mlocks */
    for (int i = 0; i < LOCK_NUM; i++) {
        mlocks[i].lock.status = UNLOCKED;
        mlocks[i].key = 0;
        init_list_head(&mlocks[i].block_queue);
    }
}

/* initialize spin lock */
void spin_lock_init(spin_lock_t *lock) { lock->status = UNLOCKED; }

/* try to acquire spin lock */
int spin_lock_try_acquire(spin_lock_t *lock) { return (atomic_swap(LOCKED, (ptr_t)&lock->status) == UNLOCKED); }

/* acquire spin lock */
void spin_lock_acquire(spin_lock_t *lock) { while (atomic_swap(LOCKED, (ptr_t)&lock->status) == LOCKED); }

/* release spin lock */
void spin_lock_release(spin_lock_t *lock) { lock->status = UNLOCKED; }

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

void init_barriers(void) {
    for (int i = 0; i < BARRIER_NUM; i++) {
        barrs[i].goal = 0;
        barrs[i].wait_num = 0;
        barrs[i].wait_list.prev = barrs[i].wait_list.next = &barrs[i].wait_list;
        barrs[i].key = 0;
        barrs[i].usage = UNUSED;
    }
}

int do_barrier_init(int key, int goal) {
    for (int i = 0; i < BARRIER_NUM; i++) {
        // 必须同时满足：正在使用中 (USING) 且 Key 匹配
        if (barrs[i].usage == USING && barrs[i].key == key) {
            // 【重要】: 如果找到了，直接返回该索引。
            // 注意：这里我们通常忽略参数中的 `goal`
            // 因为屏障已经被第一个进程创建并设定了 goal，
            // 后来的进程只是获取句柄来使用它。
            return i;
        }
    }
    for (int i = 0; i < BARRIER_NUM; i++) {
        if (barrs[i].usage == UNUSED) {
            barrs[i].usage = USING; // 标记为正在使用
            barrs[i].key = key;     // 设置 Key
            barrs[i].goal = goal;   // 设置目标等待数量
            barrs[i].wait_num = 0;  // 当前已到达数量清零

            // 初始化等待队列（双向链表指向自己）
            barrs[i].wait_list.prev = barrs[i].wait_list.next = &barrs[i].wait_list;

            return i; // 返回新创建的屏障索引
        }
    }
    return -1;
} /* 本来认为如果有两个进程申请相同key会破坏唯一性，但是在当前任务中没有这个情况，不考虑，所以没有上半部分。后来想了一下不对 */

void do_barrier_wait(int bar_idx) {
    barrs[bar_idx].wait_num++;
    if (barrs[bar_idx].goal != barrs[bar_idx].wait_num) {
        do_block(&current_running->list, &barrs[bar_idx].wait_list);
        do_scheduler();
    } else {
        free_block_list(&barrs[bar_idx].wait_list);
        barrs[bar_idx].wait_num = 0;
    }
}

void do_barrier_destroy(int bar_idx) {
    free_block_list(&barrs[bar_idx].wait_list);
    barrs[bar_idx].goal = 0;
    barrs[bar_idx].wait_num = 0;
    barrs[bar_idx].key = 0;
    barrs[bar_idx].usage = UNUSED;
}

void init_conditions(void) {
    for (int i = 0; i < CONDITION_NUM; i++) {
        conds[i].wait_list.prev = conds[i].wait_list.next = &conds[i].wait_list; // 初始化等待队列
        conds[i].key = 0;
        conds[i].usage = UNUSED;
    }
}

int do_condition_init(int key) {
    // ---------------------------------------------------------
    // 1. 检查是否已有相同 Key 的条件变量
    // ---------------------------------------------------------
    for (int i = 0; i < CONDITION_NUM; i++) {
        if (conds[i].usage == USING && conds[i].key == key) {
            return i; // 握手成功，返回同一个句柄
        }
    }

    // ---------------------------------------------------------
    // 2. 如果没找到，寻找一个空闲槽位创建新的
    // ---------------------------------------------------------
    for (int i = 0; i < CONDITION_NUM; i++) {
        if (conds[i].usage == UNUSED) {
            conds[i].usage = USING;
            conds[i].key = key;

            // 初始化等待队列
            conds[i].wait_list.prev = conds[i].wait_list.next = &conds[i].wait_list;

            return i;
        }
    }

    // 3. 满了
    return -1;
}

void do_condition_wait(int cond_idx, int mutex_idx) {
    current_running->status = TASK_BLOCKED;
    add_node_to_q(&current_running->list, &conds[cond_idx].wait_list);
    do_mutex_lock_release(mutex_idx);
    do_scheduler();
    do_mutex_lock_acquire(mutex_idx);
}

void do_condition_signal(int cond_idx) {
    list_node_t *head = &conds[cond_idx].wait_list, *p = head->next;
    if (p != head) {
        do_unblock(p);
    }
}

void do_condition_broadcast(int cond_idx) { free_block_list(&conds[cond_idx].wait_list); }
void do_condition_destroy(int cond_idx) {
    do_condition_broadcast(cond_idx);
    conds[cond_idx].key = 0;
    conds[cond_idx].usage = UNUSED;
}

void init_semaphores(void) {
    for (int i = 0; i < SEMAPHORE_NUM; i++) {
        semaphores[i].sem = 0;
        semaphores[i].wait_list.prev = semaphores[i].wait_list.next = &semaphores[i].wait_list;
        semaphores[i].key = 0;
        semaphores[i].usage = UNUSED;
    }
}

int do_semaphore_init(int key, int init) {
    // 1. Check if a semaphore with this key already exists
    for (int i = 0; i < SEMAPHORE_NUM; i++) {
        if (semaphores[i].usage == USING && semaphores[i].key == key) {
            return i;
        }
    }

    // 2. Find an unused semaphore slot
    for (int i = 0; i < SEMAPHORE_NUM; i++) {
        if (semaphores[i].usage == UNUSED) {
            semaphores[i].usage = USING;
            semaphores[i].key = key;
            semaphores[i].sem = init;
            // Ensure list is clean
            semaphores[i].wait_list.prev = semaphores[i].wait_list.next = &semaphores[i].wait_list;
            return i;
        }
    }

    return -1; // No free semaphores
}

void do_semaphore_up(int sema_idx) {
    semaphore_t *s = &semaphores[sema_idx];
    s->sem++;

    // If sem <= 0 after increment, it means tasks were waiting (sem was negative)
    if (s->sem <= 0) {
        list_node_t *head = &s->wait_list;
        list_node_t *p = head->next;

        // Unblock the first waiting task
        if (p != head) {
            do_unblock(p);
        }
    }
}

void do_semaphore_down(int sema_idx) {
    semaphore_t *s = &semaphores[sema_idx];
    s->sem--;

    // If sem < 0, the resource is exhausted, block current task
    if (s->sem < 0) {
        do_block(&current_running->list, &s->wait_list);
        do_scheduler();
    }
}

void do_semaphore_destroy(int sema_idx) {
    // Wake up all tasks waiting on this semaphore to avoid deadlocks
    free_block_list(&semaphores[sema_idx].wait_list);

    semaphores[sema_idx].usage = UNUSED;
    semaphores[sema_idx].key = 0;
    semaphores[sema_idx].sem = 0;
}

void init_mbox() {
    for (int i = 0; i < MBOX_NUM; i++) {
        mbox[i].name[0] = '\0';
        mbox[i].wcur = 0;
        mbox[i].rcur = 0;
        mbox[i].user_num = 0;
        mbox[i].wait_mbox_full.prev = mbox[i].wait_mbox_full.next = &mbox[i].wait_mbox_full;
        mbox[i].wait_mbox_empty.prev = mbox[i].wait_mbox_empty.next = &mbox[i].wait_mbox_empty;
    }
}
int do_mbox_open(char *name) {
    /*根据名字返回一个对应的 mailbox 如果不存在对应名字的 mailbox 则返回一个新的 mailbox*/
    for (int i = 0; i < MBOX_NUM; i++) {
        if (strcmp(mbox[i].name, name) == 0) {
            mbox[i].user_num++;
            return i;
        }
    }

    for (int i = 0; i < MBOX_NUM; i++) {
        if (mbox[i].name[0] == '\0') {
            strcpy(mbox[i].name, name);
            mbox[i].user_num++;
            return i;
        }
    }
    return -1;
}

void do_mbox_close(int mbox_idx) {
    mbox[mbox_idx].user_num--;
    if (mbox[mbox_idx].user_num == 0) {
        mbox[mbox_idx].name[0] = '\0';
        mbox[mbox_idx].wcur = 0;
        mbox[mbox_idx].rcur = 0;
    }
}

static void circular_buffer_copy(char *dest, const char *src, uint32_t base_cur, int len, int buffer_size, copy_mode_t mode) {
    uint32_t physical_idx; // 使用无符号数计算物理下标
    // 这里可以拆分成两次 memcpy，但为了可读性，循环足矣
    if (mode == COPY_TO_MBOX) {
        // 写操作: src 是线性的 (用户数据), dest 是环形的 (邮箱 buffer)
        for (int i = 0; i < len; i++) {
            physical_idx = (base_cur + i) % buffer_size;
            dest[physical_idx] = src[i];
        }
    } else {
        // 读操作: src 是环形的 (邮箱 buffer), dest 是线性的 (用户 buffer)
        for (int i = 0; i < len; i++) {
            physical_idx = (base_cur + i) % buffer_size;
            dest[i] = src[physical_idx];
        }
    }
}

int do_mbox_send(int mbox_idx, void *msg, int msg_length) {
    // 1. 使用指针别名，减少代码噪音
    mailbox_t *mb = &mbox[mbox_idx];
    int block_count = 0;

    // 2. 利用无符号减法计算当前已用空间
    // 无论 wcur 是否溢出回绕，(wcur - rcur) 永远等于缓冲区内的数据量
    while (1) {
        uint32_t bytes_used = mb->wcur - mb->rcur;
        uint32_t bytes_free = MAX_MBOX_LENGTH - bytes_used;

        // 如果剩余空间足够，跳出等待
        if (bytes_free >= msg_length) {
            break;
        }

        // 空间不足，阻塞
        do_block(&current_running->list, &mb->wait_mbox_full);
        do_scheduler();
        block_count++;
    }

    // 3. 执行拷贝
    circular_buffer_copy(mb->msg, (const char *)msg, mb->wcur, msg_length,MAX_MBOX_LENGTH, COPY_TO_MBOX);

    // 4. 更新游标
    mb->wcur += msg_length; // 无符号加法，自动处理溢出回绕

    // 5. 唤醒等待数据的进程
    free_block_list(&mb->wait_mbox_empty);

    return block_count;
}

int do_mbox_recv(int mbox_idx, void *msg, int msg_length) {
    mailbox_t *mb = &mbox[mbox_idx];
    int block_count = 0;

    while (1) {
        // 计算当前缓冲区内有多少数据可读
        uint32_t bytes_available = mb->wcur - mb->rcur;

        // 如果数据量足够，跳出等待
        if (bytes_available >= msg_length) {
            break;
        }

        // 数据不足，阻塞
        do_block(&current_running->list, &mb->wait_mbox_empty);
        do_scheduler();
        block_count++;
    }

    // 执行拷贝
    circular_buffer_copy((char *)msg, mb->msg, mb->rcur, msg_length, 
                         MAX_MBOX_LENGTH, COPY_FROM_MBOX);
    
    // 更新游标 (注意：这里不需要取模，让它一直增加即可)
    mb->rcur += msg_length;

    // 唤醒等待空间的发送进程
    free_block_list(&mb->wait_mbox_full);
    
    return block_count;
}