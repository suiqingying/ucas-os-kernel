#include <kernel.h>
#include <stdint.h>
#include <syscall.h>
#include <unistd.h>

static const long IGNORE = 0L;

static long invoke_syscall(long sysno, long arg0, long arg1, long arg2,
                           long arg3, long arg4) {
    /* implement invoke_syscall via inline assembly */
    // 对应 RISC-V 调用约定，a0-a4 是参数，a7 是系统调用号
    register long a0_reg asm("a0") = arg0;
    register long a1_reg asm("a1") = arg1;
    register long a2_reg asm("a2") = arg2;
    register long a3_reg asm("a3") = arg3;
    register long a4_reg asm("a4") = arg4;
    register long a7_reg asm("a7") = sysno;

    asm volatile(
        "ecall"
        : "+r"(a0_reg) /* a0 is in/out: result returned in a0 */
        : "r"(a1_reg), "r"(a2_reg), "r"(a3_reg), "r"(a4_reg), "r"(a7_reg)
        : "memory");

    return a0_reg;
}

void sys_yield(void) {
    /* call invoke_syscall to implement sys_yield */
    invoke_syscall(SYSCALL_YIELD, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_move_cursor(int x, int y) {
    /* call invoke_syscall to implement sys_move_cursor */
    invoke_syscall(SYSCALL_CURSOR, (long)x, (long)y, IGNORE, IGNORE, IGNORE);
}

void sys_write(char *buff) {
    /* call invoke_syscall to implement sys_write */
    invoke_syscall(SYSCALL_WRITE, (long)buff, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_reflush(void) {
    /* call invoke_syscall to implement sys_reflush */
    invoke_syscall(SYSCALL_REFLUSH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mutex_init(int key) {
    /* call invoke_syscall to implement sys_mutex_init */
    return invoke_syscall(SYSCALL_LOCK_INIT, (long)key, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mutex_acquire(int mutex_idx) {
    /* call invoke_syscall to implement sys_mutex_acquire */
    invoke_syscall(SYSCALL_LOCK_ACQ, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mutex_release(int mutex_idx) {
    /* call invoke_syscall to implement sys_mutex_release */
    invoke_syscall(SYSCALL_LOCK_RELEASE, (long)mutex_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

long sys_get_timebase(void) {
    /* call invoke_syscall to implement sys_get_timebase */
    return invoke_syscall(SYSCALL_GET_TIMEBASE, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

long sys_get_tick(void) {
    /* call invoke_syscall to implement sys_get_tick */
    return invoke_syscall(SYSCALL_GET_TICK, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_sleep(uint32_t time) {
    /* call invoke_syscall to implement sys_sleep */
    invoke_syscall(SYSCALL_SLEEP, (long)time, IGNORE, IGNORE, IGNORE, IGNORE);
}

pid_t sys_exec(char *name, int argc, char **argv) {
    /* call invoke_syscall to implement sys_exec */
    return invoke_syscall(SYSCALL_EXEC, (long)name, (long)argc, (long)argv, IGNORE, IGNORE);
}

void sys_list() {
    /* call invoke_syscall to implement sys_list */
    invoke_syscall(SYSCALL_LIST, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}
void sys_exit(void) {
    /* call invoke_syscall to implement sys_exit */
    invoke_syscall(SYSCALL_EXIT, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_kill(pid_t pid) {
    /* call invoke_syscall to implement sys_kill */
    return invoke_syscall(SYSCALL_KILL, (long)pid, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_waitpid(pid_t pid) {
    /* call invoke_syscall to implement sys_waitpid */
    return invoke_syscall(SYSCALL_WAITPID, (long)pid, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_ps(void) {
    /* call invoke_syscall to implement sys_ps */
    invoke_syscall(SYSCALL_PS, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

pid_t sys_getpid() {
    /* call invoke_syscall to implement sys_getpid */
    return invoke_syscall(SYSCALL_GETPID, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_getchar(void) {
    /* call invoke_syscall to implement sys_getchar */
    return invoke_syscall(SYSCALL_READCH, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_write_ch(char c) {
    invoke_syscall(SYSCALL_WRITECH, c, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_clear(void) {
    invoke_syscall(SYSCALL_CLEAR, IGNORE, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_barrier_init(int key, int goal) {
    /* call invoke_syscall to implement sys_barrier_init */
    return invoke_syscall(SYSCALL_BARR_INIT, (long)key, (long)goal, IGNORE, IGNORE, IGNORE);
}

void sys_barrier_wait(int bar_idx) {
    /* call invoke_syscall to implement sys_barrie_wait */
    invoke_syscall(SYSCALL_BARR_WAIT, (long)bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_barrier_destroy(int bar_idx) {
    /* call invoke_syscall to implement sys_barrier_destroy */
    invoke_syscall(SYSCALL_BARR_DESTROY, (long)bar_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_condition_init(int key) {
    /* call invoke_syscall to implement sys_condition_init */
    return invoke_syscall(SYSCALL_COND_INIT, (long)key, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_wait(int cond_idx, int mutex_idx) {
    /* call invoke_syscall to implement sys_condition_wait */
    invoke_syscall(SYSCALL_COND_WAIT, (long)cond_idx, (long)mutex_idx, IGNORE, IGNORE, IGNORE);
}

void sys_condition_signal(int cond_idx) {
    /* call invoke_syscall to implement sys_condition_signal */
    invoke_syscall(SYSCALL_COND_SIGNAL, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_broadcast(int cond_idx) {
    /* call invoke_syscall to implement sys_condition_broadcast */
    invoke_syscall(SYSCALL_COND_BROADCAST, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_condition_destroy(int cond_idx) {
    /* call invoke_syscall to implement sys_condition_destroy */
    invoke_syscall(SYSCALL_COND_DESTROY, (long)cond_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_semaphore_init(int key, int init) {
    /* call invoke_syscall to implement sys_semaphore_init */
    return invoke_syscall(SYSCALL_SEMA_INIT, (long)key, (long)init, IGNORE, IGNORE, IGNORE);
}

void sys_semaphore_up(int sema_idx) {
    /* call invoke_syscall to implement sys_semaphore_up */
    invoke_syscall(SYSCALL_SEMA_UP, (long)sema_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_semaphore_down(int sema_idx) {
    /* call invoke_syscall to implement sys_semaphore_down */
    invoke_syscall(SYSCALL_SEMA_DOWN, (long)sema_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_semaphore_destroy(int sema_idx) {
    /* call invoke_syscall to implement sys_semaphore_destroy */
    invoke_syscall(SYSCALL_SEMA_DESTROY, (long)sema_idx, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mbox_open(char *name) {
    /* call invoke_syscall to implement sys_mbox_open */
    return invoke_syscall(SYSCALL_MBOX_OPEN, (long)name, IGNORE, IGNORE, IGNORE, IGNORE);
}

void sys_mbox_close(int mbox_id) {
    /* call invoke_syscall to implement sys_mbox_close */
    invoke_syscall(SYSCALL_MBOX_CLOSE, (long)mbox_id, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_mbox_send(int mbox_idx, void *msg, int msg_length) {
    /* call invoke_syscall to implement sys_mbox_send */
    return invoke_syscall(SYSCALL_MBOX_SEND, (long)mbox_idx, (long)msg, (long)msg_length, IGNORE, IGNORE);
}

int sys_mbox_recv(int mbox_idx, void *msg, int msg_length) {
    /* call invoke_syscall to implement sys_mbox_recv */
    return invoke_syscall(SYSCALL_MBOX_RECV, (long)mbox_idx, (long)msg, (long)msg_length, IGNORE, IGNORE);
}

void sys_taskset (pid_t pid, int mask) {
    /* call invoke_syscall to implement sys_taskset */
    invoke_syscall(SYSCALL_TASKSET, (long)pid, (long)mask, IGNORE, IGNORE, IGNORE);
}

int sys_thread_create(void (*start_routine)(void *), void *arg) {
    return invoke_syscall(SYSCALL_THREAD_CREATE, (long)start_routine, (long)arg, IGNORE, IGNORE, IGNORE);
}

void sys_thread_exit(void *retval) {
    invoke_syscall(SYSCALL_THREAD_EXIT, (long)retval, IGNORE, IGNORE, IGNORE, IGNORE);
}

int sys_thread_join(pid_t tid, void **retval) {
    return invoke_syscall(SYSCALL_THREAD_JOIN, (long)tid, (long)retval, IGNORE, IGNORE, IGNORE);
}
/************************************************************/
