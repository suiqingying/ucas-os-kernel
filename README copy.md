# 操作系统项目二：内核实现日志

本文档记录了在实现简易内核（Project 2）过程中的主要工作、遇到的问题以及相关的学习与思考。

## Task 1: 任务启动与非抢占式调度

### 已完成工作

1.  **进程控制块 (PCB) 初始化**: 实现了 `init_pcb` 函数，用于在内核启动时加载预设的用户任务（如 `print1`, `lock1` 等），并为它们创建和初始化PCB。
2.  **上下文初始化**: 实现了 `init_pcb_stack`，用于在每个进程的内核栈上预置初始上下文，确保进程在第一次被调度时能从其入口点正确开始执行。
3.  **就绪队列 (Ready Queue)**: 定义并初始化了一个全局的 `ready_queue`，用于管理所有处于就绪状态的进程。
4.  **非抢占式调度器**: 实现了 `do_scheduler` 的基本版本。在非抢占式模型下，它通过 `sys_yield` 等方式被动触发，从就绪队列中选择下一个要运行的进程。
5.  **上下文切换**: 实现了 `switch_to` 汇编函数，负责在进程切换时，保存上一个进程的 callee-saved 寄存器，并恢复下一个进程的寄存器，完成CPU状态的交接。

---

## Task 2: 互斥锁 (Mutex) 的实现

### 已完成工作

1.  **进程阻塞与唤醒**: 在 `sched.c` 中实现了 `do_block` 和 `do_unblock` 函数，为进程的挂起和恢复提供了基础机制。
2.  **锁的初始化**: 实现了 `init_locks` 对全局锁数组进行初始化，以及 `do_mutex_lock_init` 函数，允许用户程序通过一个 `key` 来请求和创建一把锁。
3.  **锁的获取与释放**: 实现了 `do_mutex_lock_acquire` 和 `do_mutex_lock_release` 的核心逻辑，处理了锁空闲和锁被占用两种情况下的不同行为。
4.  **自旋锁 (Spin Lock)**: 实现了 `spin_lock_acquire` 等一系列自旋锁函数，作为底层原子操作的封装，用于保护互斥锁自身数据结构在修改时的线程安全。
5.  **系统调用接口**: 在 `tiny_libc/syscall.c` 中，通过跳转表（Jumptab）为用户程序提供了 `sys_mutex_init`, `sys_mutex_acquire`, `sys_mutex_release` 等接口。

### 遇到的问题与学习点

1.  **锁的核心概念**: 深入讨论了锁的本质作用，即“排队”，以保证对共享资源的互斥访问。明确了“洗手间和钥匙”模型，为后续的理解打下了基础。

2.  **自旋锁 vs. 互斥锁**: 厘清了两种锁在“等待”策略上的根本区别：
    *   **互斥锁**: “文明”地睡眠并让出CPU，适用于可能较长的等待。
    *   **自旋锁**: “焦急”地忙等待并占用CPU，仅适用于可预见的、极短的等待。

3.  **锁的嵌套实现**: 理解了“用一个轻量级的锁（自旋锁）去保护一个重量级的锁（互斥锁）的内部状态”这种分层设计思想，明确了两种锁在内核实现中的不同角色。

4.  **单核环境下的自旋锁悖论**: 探讨了在单核CPU上，用于进程间同步的自旋锁会导致死锁的问题。明确了在当前实验中，自旋锁的主要意义在于学习一个通用的、可在多核或抢占式环境下工作的编程范式，并为未来Task 4中引入的中断处理提供保护。

5.  **`release` 逻辑的细节**: 在实现 `do_mutex_lock_release` 时，围绕“当有进程在等待时，是应该解锁还是转移所有权”这一核心逻辑进行了反复的讨论和修正，最终确定了“所有权转移”（不改变锁状态，直接唤醒下一个进程）的正确模型，并修复了相关的并发和逻辑错误。

---
## Project 2, Task 3: 系统调用 (System Calls)

### 已完成工作

1.  **异常/中断入口设置**:
    *   在 `init/main.c` 中调用 `init_exception()`，并在 `kernel/irq/irq.c` 中实现了该函数，用于初始化异常处理表 `exc_table`。
    *   在 `arch/riscv/kernel/trap.S` 中实现了 `setup_exception`，将 `exception_handler_entry` 的地址写入了 `stvec` 寄存器，使得所有 S-mode 的异常和中断都会跳转到我们设定的入口点。

2.  **上下文保存与恢复**:
    *   在 `arch/riscv/kernel/entry.S` 中，实现了 `SAVE_CONTEXT` 和 `RESTORE_CONTEXT` 两个核心汇编宏。
    *   `SAVE_CONTEXT`: 在进入异常处理前，在内核栈上精确地保存所有32个通用寄存器以及 `sstatus`, `sepc` 等CSR寄存器，为内核提供一个安全、独立的执行环境。
    *   `RESTORE_CONTEXT`: 在异常处理返回前，从内核栈上恢复所有寄存器，确保用户程序能从被中断的地方无缝继续执行。

3.  **异常分发与返回**:
    *   实现了 `exception_handler_entry`，作为异常处理的统一入口，负责调用 C 函数 `interrupt_helper`。
    *   实现了 `interrupt_helper`，它根据 `scause` 的值来区分是异常（Exception）还是中断（Interrupt），并将异常分发给 `handle_syscall`。
    *   实现了 `ret_from_exception`，在异常处理结束后，通过 `sret` 指令安全地返回用户态。

4.  **系统调用接口实现**:
    *   **用户态**: 在 `tiny_libc/syscall.c` 中，实现了 `invoke_syscall` 函数，它通过 `ecall` 指令触发异常，是用户程序进入内核的桥梁。同时，将所有 `sys_*` 函数（如 `sys_sleep`）都修改为通过 `invoke_syscall` 来实现。
    *   **内核态**: 在 `kernel/syscall/syscall.c` 中，实现了 `handle_syscall` 函数。它从保存的上下文中获取系统调用号和参数，并通过一个函数指针数组 `syscall[]` 来调用对应的内核函数（如 `do_sleep`）。

5.  **`sleep` 系统调用实现**:
    *   实现了 `do_sleep` 函数，它通过设置进程的 `wakeup_time` 并调用调度器来让当前进程“睡眠”。
    *   实现了 `check_sleeping` 函数，它在每次调度前被调用，用于遍历 `sleep_queue`，将所有已经“睡醒”的进程唤醒并移入就绪队列。

### 遇到的问题与学习点

1.  **`do_scheduler` 的逻辑缺陷**:
    *   在深入探讨中，我们发现了一个长期存在的逻辑问题：`do_scheduler` 函数会将所有 `TASK_BLOCKED` 状态的进程都放入 `sleep_queue`。
    *   我们最终确认，这个缺陷仅影响了 `do_sleep` 的实现（导致了“假睡眠”问题），而 `do_mutex_lock_acquire` 因为实现了自己的“迷你调度器”，没有调用 `do_scheduler`，所以完美地避开了这个问题。
    *   通过修正 `do_scheduler` 和 `do_sleep` 的代码，我们理清了“谁阻塞，谁负责入队”的设计原则。

2.  **`do_mutex_lock_acquire` 的工作原理**:
    *   我们进行了非常深入的模拟和分析，最终确认了用户当前的 `do_mutex_lock_acquire` 实现的有效性。
    *   它并非一个传统的互斥锁，而是通过将一个**自旋锁**包装成了一个**阻塞锁**。互斥性由底层的自旋锁保证，而阻塞和唤醒则由 `block_queue` 和调度共同完成。
    *   我们确认了，在当前的协作式、单核环境下，这个实现是有效的，并解释了为什么它在测试中表现为“等待锁”而不是发生数据竞争。

3.  **上下文切换的细节**:
    *   通过对“进程被唤醒后立即执行是否会导致错误”这一问题的反复探讨，我们澄清了内核上下文（由 `switch_to` 保存）和用户上下文（由 `SAVE_CONTEXT` 保存）的区别。
    *   最终明确，唤醒操作只改变进程的逻辑状态，而不会破坏其保存在内核栈上的任何上下文快照，因此 `switch_to` 的恢复过程是绝对安全的。

---
 ## Task 4: 抢占式调度 (Preemptive Scheduling)
### 已完成工作

1.  **开启中断**: 为了让内核能够响应中断，我们进行了两级中断使能：
    *   **全局中断使能**: 在 `arch/riscv/kernel/trap.S` 的 `setup_exception`
      函数中，通过以下指令打开了S模式的全局中断总开关：
          csrs sstatus, SR_SIE

   1     *   **定时器中断源使能**: 在 `init/main.c` 中，通过调用 `enable_preempt()` 函数，打开了 `sie`
     寄存器中所有S模式中断源的开关，其中包含了我们需要的 `STIE`（S模式定时器中断使能）。
   2
   3 2.  **中断处理与分发**:
   4     *   在 `kernel/irq/irq.c` 的 `init_exception` 函数中，我们将 `irq_table` 中断向量表中的对应项设置为
     `handle_irq_timer` 函数的地址：
          irq_table[IRQC_S_TIMER] = handle_irq_timer;

   1     *   `interrupt_helper` 函数现在可以根据 `scause` 寄存器的最高位判断出中断的发生，并根据中断类型码，通过
     `irq_table` 准确地调用 `handle_irq_timer`。
   2
   3 3.  **定时器中断处理**:
   4     *   在 `handle_irq_timer` 中，我们实现了抢占式调度的核心逻辑。它主要负责两件事：
   5         1.  调用 `bios_set_timer()` 来设置下一次定时器中断，确保时钟心跳的持续。
   6         2.  调用 `do_scheduler()` 来强制触发任务调度，实现“抢占”。
          void handle_irq_timer(regs_context_t *regs, uint64_t stval, uint64_t scause)
          {
              bios_set_timer(get_ticks() + TIMER_INTERVAL);
              do_scheduler();
          }

   1
   2 4.  **内核空闲循环**:
   3     *   修改了 `init/main.c` 中的主循环，注释掉了 `do_scheduler()` 的主动调用，并替换为 `asm
     volatile("wfi");`
     指令。这使得CPU在没有任务执行时，可以进入低功耗的等待中断状态，这是抢占式内核正确的空闲处理方式。
          while (1) {
              asm volatile("wfi");
          }

    1
    2 ### 遇到的问题与学习点
    3
    4 1.  **`sstatus.SIE` vs `sie.STIE`**:
    5     *   在讨论中，我们厘清了这两个关键控制位的区别。`sstatus` 中的 `SIE` 位是S模式中断的“总电闸”，而 `sie`
      寄存器中的 `STIE`
      位是定时器中断这一个“分路开关”。必须同时打开两者，定时器中断才能正常触发。这个“总闸-分闸”的比喻帮助我们深
      刻理解了RISC-V的中断控制机制。
    6
    7 2.  **`csrs` vs `csrw`**:
    8     *   我们详细探讨了这两条指令的差异。`csrs` (set) 执行的是按位或操作（`sstatus = sstatus | SR_SIE`
      ），只设置目标位而不影响其他位，是精确的“开启”操作。而 `csrw` (write) 执行的是覆盖写操作（`sstatus =
      SR_SIE`），会清除其他所有位，这对于 `sstatus`
      这样的多状态复合寄存器是灾难性的。这让我们明确了在修改CSR时，必须谨慎选择原子操作指令。
    9
   10 ---
   11
   12 ## Task 5: 复杂调度算法 (Complex Scheduling Algorithm)
   13
   14 ### 已完成工作
   15
   16 1.  **`set_sche_workload` 系统调用**:
   17     *   为了让用户程序能向内核报告进度，我们完整地实现了一个新的系统调用。这包括在 `unistd.h` 和
      `syscall.h` 中定义系统调用号 `SYSCALL_SET_SCHE_WORKLOAD`，在 `tiny_libc` 中提供 `sys_set_sche_workload`
      用户接口，并在内核的 `init_syscall` 中注册了 `do_set_sche_workload` 处理函数。
   18
   19 2.  **内核态进度追踪 (圈数统计)**:
   20     *   在 `pcb_t` 结构体中增加了 `last_workload` 和 `lap_count` 两个成员。
   21     *   重写了 `do_set_sche_workload` 函数，使其不再是简单的赋值。它通过比较本次和上一次的 `workload`
      值，能够智能地检测到飞机循环运动导致的“回环”现象。一旦检测到回环，就将 `lap_count` 加一。
   22     *   通过以下核心逻辑，内核成功地将被循环重置的坐标值，“解算”成了一个能真实反映总进度的、单调递增的
      `workload` 值，并将其存回 `pcb->workload`。
          // In do_set_sche_workload(current_workload)
          if (current_workload < task->last_workload &&
              (task->last_workload - current_workload) > (SCREEN_WIDTH / 2)) {
              task->lap_count++;
          }
          uint64_t true_workload = (task->lap_count * SCREEN_WIDTH) + current_workload;
          task->workload = true_workload;
          task->last_workload = current_workload;
   1
   2 3.  **加权轮转调度 (Weighted Round-Robin)**:
   3     *   **时间片概念引入**: 在 `pcb_t` 中增加了 `time_slice` (时间片配额) 和 `time_slice_remain`
     (剩余时间片) 成员。
   4     *   **中断处理修改**: `handle_irq_timer`
     的逻辑被彻底修改。它不再是每次都触发调度，而是作为“倒计时器”，每次中断只将当前任务的 `time_slice_remain`
     减一。只有当 `time_slice_remain` 耗尽为0时，才调用 `do_scheduler`。
          // In handle_irq_timer()
          if (current_running->time_slice_remain > 0) {
              current_running->time_slice_remain--;
          }
          if (current_running->time_slice_remain == 0) {
              do_scheduler();
          }
   1     *   **调度器修改**: `do_scheduler` 在选出下一个任务后，会负责将该任务的 `time_slice_remain` 重置为它的
     `time_slice` 配额。
          // In do_scheduler()
          current_running = get_pcb_from_node(next_node);
          current_running->time_slice_remain = current_running->time_slice;

   1
   2 4.  **动态时间片计算**:
   3     *   实现了 `update_time_slices` 函数。该函数作为“会计”，在每次调度前被调用。
   4     *   它通过遍历所有就绪任务，找出 `workload` 最大的（跑得最快的），然后根据每个任务与最快者之间的差距
     `lag`，动态地为每个任务计算出下一轮的时间片配额。
          // In update_time_slices()
          int64_t lag = max_workload - pcb->workload;
          pcb->time_slice = (lag / 30) + 1;

    1
    2 ### 遇到的问题与学习点
    3
    4 1.  **调度算法的初步探索与修正**:
    5     *   我们最初构思的“总是选择 `workload`
      最大的任务”是一个严格优先级算法。经过讨论，我们很快意识到这会导致任务饥饿，无法满足“所有飞机都在飞”的视觉
      要求，因此果断放弃了该思路，转向了更公平的加权轮转模型。
    6
    7 2.  **“循环Workload”问题与内核态解算**:
    8     *   这是本次任务中最有价值的发现。我们意识到，用户程序基于屏幕坐标的循环 `workload`
      是一个有缺陷的、非线性的进度指标，它会严重误导调度器。
    9     *   我们没有选择修改用户程序，而是在内核的 `do_set_sche_workload`
      中设计并实现了“圈数统计”逻辑，成功地在内核态将被污染的数据“清洗”和“还原”成了真实、单调的进度值。这体现了操
      作系统作为底层服务，应具备兼容和适应上层应用的能力。
   10
   11 3.  **调度器参数调优 (Tuning)**:
   12     *   我们深入探讨了 `TIMER_INTERVAL` 和时间片计算公式中“魔法数字”的意义。
   13     *   明确了 `TIMER_INTERVAL` 定义了调度的“精度”和“开销”之间的平衡。我们通过实验，最终选择了 `(time_base
      / 5000)` (即0.2ms) 作为时钟频率，因为它在提供了流畅视觉效果的同时，也在可接受的开销范围内。
          #define TIMER_INTERVAL (time_base / 5000)

   1     *   同时，`pcb->time_slice = (lag / 30) + 1;` 中的 `30` 和 `1` 也是调优的结果。`+1`
     保证了即使是最领先的任务也能获得最小时间片，不会“停下”；而 `30`
     这个系数则控制了落后任务“追赶”的积极程度。整个过程让我们体会到了操作系统设计中，理论结合实验调优的重要性。