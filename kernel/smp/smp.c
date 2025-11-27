#include <atomic.h>
#include <os/kernel.h>
#include <os/lock.h>
#include <os/sched.h>
#include <os/smp.h>
#include <printk.h>

// 将启动屏障封装在 smp.c 内部，使其成为私有实现细节
static volatile int boot_barrier = 0;

// 声明一个全局的大内核锁变量
static spin_lock_t kernel_lock;

void smp_init()
{
    // 初始化内核大锁
    spin_lock_init(&kernel_lock);

    // 初始化启动屏障
    boot_barrier = 0;
    printk("> [SMP] SMP locks and boot barrier initialized.\n");
}

void wakeup_other_hart()
{
    printk("> [SMP] Hart 0 is waking up other harts...\n");
    // 通过修改屏障变量来“唤醒”正在自旋等待的从核
    boot_barrier = 1;
}

void smp_wait_for_boot()
{
    // 从核在此处自旋，等待主核调用 wakeup_other_hart()
    while (boot_barrier == 0);
}

void lock_kernel() { spin_lock_acquire(&kernel_lock); }

void unlock_kernel() { spin_lock_release(&kernel_lock); }
