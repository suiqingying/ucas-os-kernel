#include <atomic.h>
#include <os/list.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/string.h>
#include <printk.h>
#include <pgtable.h>
#include <os/mm.h>

#define PAGE_SIZE 4096
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
    // 获取锁失败，阻塞当前进程并调度
    do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
    do_scheduler();
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
        // Clear the message buffer
        memset(mbox[i].msg, 0, MAX_MBOX_LENGTH + 1);
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
            // Initialize the new mailbox
            mbox[i].wcur = 0;
            mbox[i].rcur = 0;
            mbox[i].user_num = 1;  
            // Clear the message buffer for the new mailbox
            memset(mbox[i].msg, 0, MAX_MBOX_LENGTH + 1);
            // Reinitialize wait queues to be safe
            mbox[i].wait_mbox_full.prev = mbox[i].wait_mbox_full.next = &mbox[i].wait_mbox_full;
            mbox[i].wait_mbox_empty.prev = mbox[i].wait_mbox_empty.next = &mbox[i].wait_mbox_empty;
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

static int map_user_page(uintptr_t user_va, int write, char **page_kva) {
    uintptr_t aligned = ROUNDDOWN(user_va, PAGE_SIZE);

    while (1) {
        uintptr_t pte_ptr = get_pteptr_of(aligned, current_running->pgdir);
        if (!pte_ptr) {
            if (!write) {
                return -1;
            }
            if (!alloc_page_helper(aligned, current_running->pgdir)) {
                return -1;
            }
            continue;
        }

        PTE *pte = (PTE *)pte_ptr;
        if (*pte & _PAGE_PRESENT) {
            *page_kva = (char *)pa2kva(get_pa(*pte));
            return 0;
        }

        if (*pte & _PAGE_SOFT) {
            int swap_idx = (int)get_pfn(*pte);
            swap_in_page(aligned, current_running->pgdir, swap_idx);
            continue;
        }

        if (!write) {
            return -1;
        }

        if (!alloc_page_helper(aligned, current_running->pgdir)) {
            return -1;
        }
    }
}

static int copy_from_user_safe(void *dst, uintptr_t user_src, uint32_t len) {
    char *d = (char *)dst;
    while (len > 0) {
        char *page_kva;
        if (map_user_page(user_src, 0, &page_kva) != 0) {
            return -1;
        }
        uint32_t offset = user_src & (PAGE_SIZE - 1);
        uint32_t chunk = PAGE_SIZE - offset;
        if (chunk > len) {
            chunk = len;
        }
        memcpy(d, page_kva + offset, chunk);
        d += chunk;
        user_src += chunk;
        len -= chunk;
    }
    return 0;
}

static int copy_to_user_safe(uintptr_t user_dst, const void *src, uint32_t len) {
    const char *s = (const char *)src;
    while (len > 0) {
        char *page_kva;
        if (map_user_page(user_dst, 1, &page_kva) != 0) {
            return -1;
        }
        uint32_t offset = user_dst & (PAGE_SIZE - 1);
        uint32_t chunk = PAGE_SIZE - offset;
        if (chunk > len) {
            chunk = len;
        }
        memcpy(page_kva + offset, s, chunk);
        s += chunk;
        user_dst += chunk;
        len -= chunk;
    }
    return 0;
}

int do_mbox_send(int mbox_idx, void *msg, int msg_length) {
    mailbox_t *mb = &mbox[mbox_idx];
    uint32_t total_sent = 0;
    char *user_ptr = (char *)msg;

    // printk("[MBOX] send start idx=%d len=%d\n", mbox_idx, msg_length);

    while (total_sent < (uint32_t)msg_length) {
        uint32_t bytes_used = mb->wcur - mb->rcur;
        uint32_t bytes_free = MAX_MBOX_LENGTH - bytes_used;

        while (bytes_free == 0) {
            do_block(&current_running->list, &mb->wait_mbox_full);
            do_scheduler();
            bytes_used = mb->wcur - mb->rcur;
            bytes_free = MAX_MBOX_LENGTH - bytes_used;
        }

        uint32_t to_send = msg_length - total_sent;
        if (to_send > bytes_free) {
            to_send = bytes_free;
        }

        uint32_t ring_pos = mb->wcur % MAX_MBOX_LENGTH;
        uint32_t first = to_send;
        uint32_t chunk_to_end = MAX_MBOX_LENGTH - ring_pos;
        if (first > chunk_to_end) {
            first = chunk_to_end;
        }

        if (copy_from_user_safe(mb->msg + ring_pos,
                                 (uintptr_t)user_ptr + total_sent,
                                 first) != 0) {
            // printk("[MBOX] ERROR: send copy failed (idx=%d)\n", mbox_idx);
            return -1;
        }

        uint32_t remaining = to_send - first;
        if (remaining > 0) {
            if (copy_from_user_safe(mb->msg,
                                     (uintptr_t)user_ptr + total_sent + first,
                                     remaining) != 0) {
                // printk("[MBOX] ERROR: send copy failed (wrap idx=%d)\n", mbox_idx);
                return -1;
            }
        }

        mb->wcur += to_send;
        total_sent += to_send;

        free_block_list(&mb->wait_mbox_empty);
    }

    // printk("[MBOX] send done idx=%d len=%d total=%u\n", mbox_idx, msg_length, total_sent);
    return (int)total_sent;
}

int do_mbox_recv(int mbox_idx, void *msg, int msg_length) {
    mailbox_t *mb = &mbox[mbox_idx];
    uint32_t total_recv = 0;
    char *user_ptr = (char *)msg;

    // printk("[MBOX] recv start idx=%d len=%d\n", mbox_idx, msg_length);

    while (total_recv < (uint32_t)msg_length) {
        uint32_t bytes_available = mb->wcur - mb->rcur;

        while (bytes_available == 0) {
            do_block(&current_running->list, &mb->wait_mbox_empty);
            do_scheduler();
            bytes_available = mb->wcur - mb->rcur;
        }

        uint32_t to_recv = msg_length - total_recv;
        if (to_recv > bytes_available) {
            to_recv = bytes_available;
        }

        uint32_t ring_pos = mb->rcur % MAX_MBOX_LENGTH;
        uint32_t first = to_recv;
        uint32_t chunk_to_end = MAX_MBOX_LENGTH - ring_pos;
        if (first > chunk_to_end) {
            first = chunk_to_end;
        }

        if (copy_to_user_safe((uintptr_t)user_ptr + total_recv,
                               mb->msg + ring_pos,
                               first) != 0) {
            // printk("[MBOX] ERROR: recv copy failed (idx=%d)\n", mbox_idx);
            return -1;
        }

        uint32_t remaining = to_recv - first;
        if (remaining > 0) {
            if (copy_to_user_safe((uintptr_t)user_ptr + total_recv + first,
                                   mb->msg,
                                   remaining) != 0) {
                // printk("[MBOX] ERROR: recv copy failed (wrap idx=%d)\n", mbox_idx);
                return -1;
            }
        }

        mb->rcur += to_recv;
        total_recv += to_recv;

        free_block_list(&mb->wait_mbox_full);
    }

    // printk("[MBOX] recv done idx=%d len=%d total=%u\n", mbox_idx, msg_length, total_recv);
    return (int)total_recv;
}
