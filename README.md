## Task 3: 多核 CPU 支持与并行执行 (Multicore Support)

### 1. 设计与实现思路

在本次任务中，我们将单核操作系统扩展为支持双核（Hart 0 为主核，Hart 1 为从核）的 SMP（对称多处理）系统。主要工作分为以下几个模块：

#### 1.1 多核启动流程重构
-   **Bootloader (`bootblock.S`)**:
    -   主核负责将内核镜像从 SD 卡加载到内存。
    -   从核通过原子变量（内存标志位）自旋等待，直到主核加载完毕并发出“放行”信号。
    -   引入 `fence.i` 指令，确保从核的指令缓存（I-Cache）与主核写入的数据缓存（D-Cache）一致，防止从核执行垃圾指令。
-   **内核入口 (`head.S`)**:
    -   通过 `csrr a0, mhartid` 识别当前核心 ID。
    -   实现了栈空间隔离：为主核分配 `P_KERNEL_STACK`，为从核分配 `S_KERNEL_STACK`。
    -   实现了上下文隔离：为主核设置 `pid0_pcb`，为从核设置 `s_pid0_pcb` 作为初始 `tp` 指针。

#### 1.2 初始化分流 (`main.c`)
-   **主核 (Master Core)**:
    -   负责所有全局资源的初始化（PCB 表、锁、屏障、系统调用表、屏幕等）。
    -   初始化完成后，通过 `wakeup_other_hart()` 修改屏障变量唤醒从核。
    -   负责启动第一个用户进程 (`shell`)。
-   **从核 (Slave Core)**:
    -   通过 `smp_wait_for_boot()` 等待主核完成全局初始化。
    -   醒来后只执行核内资源的初始化（设置 `stvec`、开启中断、设置定时器）。
    -   直接进入空闲循环 (`wfi`) 等待调度。

#### 1.3 大内核锁 (Big Kernel Lock, BKL)
-   **原理**: 采用简单的自旋锁 (`spinlock`) 保护内核态。
-   **实现 (`smp.c` & `entry.S`)**:
    -   在异常入口 `exception_handler_entry` 保存上下文后，立即调用 `lock_kernel`。
    -   在异常返回 `ret_from_exception` 恢复上下文前，调用 `unlock_kernel`。
    -   保证了任意时刻只有一个核能执行内核核心代码（如调度器），确保了并发安全。

---

### 2. 遇到的 Bug 与解决方案 (Debugging Log)

在实现多核并行的过程中，遇到了几个极其隐蔽且经典的并发/配置错误，记录如下：

#### Bug 1: 从核启动即崩溃 (Illegal Instruction at 0x50201000)
-   **现象**: 启动时主核正常，但从核报 `Illegal Instruction`，且加速比为 1（说明从核未工作）。
-   **原因**: `bootblock` 中存在 **Race Condition**。从核执行速度极快，在主核还未将内核代码完全从 SD 卡搬运到内存时，从核就跳转到了内核入口 (`0x50201000`)。此时内存中尚未初始化（全 0 或垃圾数据），导致解码错误。
-   **解决**:
    -   在 `bootblock.S` 引入一个内存标志位 `kernel_load_completed`。
    -   从核启动后自旋等待该标志位。
    -   主核加载完内核并执行 `fence.i` 后，将标志位置 1。

#### Bug 2: 新进程 (Shell) 启动死锁
-   **现象**: 双核启动成功，日志显示 Shell 已创建，但终端无反应，Shell 似乎卡死。
-   **原因**:
    -   **老任务流程**: 中断 -> `lock_kernel` -> 处理 -> `unlock_kernel` -> 返回。
    -   **新任务流程**: 调度器切换到新任务 -> 栈上 `ra` 指向 `ret_from_exception` -> 返回用户态。
    -   **问题**: 新任务跳过了 `exception_handler_entry` 后半段的解锁步骤，导致带着锁进入用户态。下次产生系统调用再次申请锁时，发生自死锁。
-   **解决**: 修改 `entry.S`，将解锁操作 `call unlock_kernel` 统一移动到 `ret_from_exception` 标签下。无论是老任务返回还是新任务启动，都必须经过此出口，从而确保锁一定被释放。

#### Bug 3: 用户程序浮点运算崩溃 (Illegal Instruction)
-   **现象**: 运行 `multicore` 测试程序时，计算加速比的 `printf` 语句导致程序崩溃。
-   **原因**: RISC-V CPU 默认禁用浮点单元 (FPU)。C 库的 `printf` 处理浮点数时生成了浮点指令，CPU 抛出异常。
-   **解决**: 在 `main.c` 初始化进程上下文 (`init_pcb_stack`) 时，修改 `sstatus` 寄存器的设置，加上 `SR_FS` 位，显式开启 FPU。

#### Bug 4: 屏幕输出乱码 (Screen Race Condition)
-   **现象**: 初始化日志与 Shell 界面混杂，出现类似 `;24H` 的乱码。
-   **原因**: 主核启动 Shell 并开启中断后，Shell 进程（用户态）尝试刷新屏幕；同时从核（内核态）尝试打印初始化日志。两者并发操作屏幕缓冲区，破坏了 ANSI 转义序列。
-   **解决**:
    -   在 `main.c` 的并发打印区域（从核唤醒后）使用 `lock_kernel()` 进行保护。
    -   调整 `enable_preempt()` 的位置，确保初始化日志打印完毕后再开启中断让 Shell 运行。

---

### 3. 实验结果
-   成功实现了主从核的协同启动与初始化。
-   `multicore` 测试程序在双核模式下运行正常。
-   **性能提升**: 相比单核模式，双核加速比达到 **~1.87x**，证明双核并行调度工作正常。