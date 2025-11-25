## Task 1: 简易终端与基础命令 (Shell & Process Basics)

### 1. Shell 设计

- `test/shell.c` 实现了一个常驻用户进程，负责刷新屏幕、读取串口输入并解析成 argv/argc。
- 支持命令：`ps`、`clear`、`ls`、`exec ... [&]`、`kill <pid>`、`wait <pid>` 以及扩展的 `taskset`。命令解析器限制 16 个参数、每个参数 16 字节，与 Project 书的“轻量 shell”要求一致。
- `exec` 通过 `sys_exec` 启动用户程序，可选择后台运行（`&`）。前台任务自动 `sys_waitpid`，后台模式只打印 PID。
- `taskset` 同时支持 “启动即绑核” 与 “-p 修改现有进程” 两种形式，shell 会在 `exec` 前暂时修改自己的亲和 mask，确保子进程继承设置。

### 2. 进程创建与基础系统调用

- **执行加载 (`do_exec`)**：`kernel/sched/sched.c` 里负责分配 PCB/栈、调用 `load_task_img()` 拷贝 ELF、将 argv 复制到用户栈（保持 128 字节对齐），最后挂到 ready queue。
- **进程结束 (`do_exit`)**：设置 `TASK_EXITED` 并调用 `pcb_release()`，释放等待队列、锁等资源，再触发调度。
- **进程终止/等待**：`do_kill()` 通过 PID 标记目标为 EXITED 并清理；`do_waitpid()` 将当前进程 block 在目标 PCB 的 wait_list，直至目标退出。
- **进程可视化 (`do_process_show`)**：打印每个 PCB 的状态、亲和 mask，并标注正在运行的 core，便于调试 shell 命令。

### 3. 屏幕与输入处理

- `screen_write`/`screen_reflush` 封装在系统调用里，shell 通过 `sys_move_cursor`、`sys_write_ch`、`sys_reflush` 等接口刷新“COMMAND”区域，使输出与 Project 文档的 UI 一致。
- 输入来自 `sys_getchar()`，支持退格、回车等基础编辑功能，确保终端交互友好。

### 4. 测试方法

1. `exec shell` 自动启动，按提示输入 `ls`/`ps` 等命令验证基本功能。
2. `exec waitpid &` 或 `exec ready_to_exit` 可测试 `exec/wait/kill` 流程。
3. `taskset 0x1 mbox_server &`、`taskset -p 0x2 <pid>` 检查亲和性命令是否生效。

---

## Task 2: 同步原语完善与 Mailbox 实现 (Synchronization & IPC)

### 1. 设计与实现思路 (Refinement & Implementation)

在本次对话中，我们对原有的同步原语进行了深度审查与重构，并完成了进程间通信（Mailbox）的健壮性实现。

#### 1.1 同步原语初始化逻辑重构 ("Lookup or Create")
-   **问题**: 原有的 `do_barrier_init` 等函数在接收到相同的 `key` 时，会错误地创建新的内核对象，导致不同进程无法通过 key 找到同一个同步原语，破坏了跨进程同步的语义。
-   **改进**:
    -   统一了所有 `init` 函数（Barrier, Semaphore, Condition, Mailbox）的逻辑。
    -   **Step 1**: 遍历数组，检查是否存在 `key` 相同且状态为 `USING` 的对象。若存在，直接返回其 ID（Handle）。
    -   **Step 2**: 若不存在，寻找 `UNUSED` 的空闲槽位进行新建初始化。
    -   这一修改确保了多进程通过 Key 进行“握手”的正确性。

#### 1.2 条件变量的原子性修复 (`do_condition_wait`)
-   **机制完善**: 标准的条件变量语义要求线程在被唤醒返回用户态时，必须持有锁。
-   **实现**: 修改 `do_condition_wait` 流程。
    1.  释放锁 (`mutex_release`)。
    2.  进入睡眠 (`scheduler`)。
    3.  **醒来后立即重新竞争锁 (`mutex_acquire`)**。
    -   只有拿到锁之后才返回用户态，防止了用户程序在无锁状态下访问共享条件（如 `num_staff`），消除了严重的数据竞争风险。

#### 1.3 邮箱通信 (Mailbox) 的环形缓冲区设计
-   **核心结构**: 采用 **Ring Buffer (环形缓冲区)** 机制。
-   **游标设计**: 使用 `unsigned int` 类型的 `wcur` (写游标) 和 `rcur` (读游标)。
    -   利用无符号整数溢出回绕（Wrap-around）的特性，放弃了复杂的取模比较。
    -   使用 `bytes_used = wcur - rcur` 计算已用空间，`bytes_free = MAX - bytes_used` 计算剩余空间，即使游标溢出也能保证计算结果正确。
-   **数据拷贝**: 封装了 `circular_buffer_copy` 辅助函数，统一处理跨越数组边界的内存拷贝逻辑（分段拷贝或取模拷贝），提升了代码可读性。

---

### 2. 遇到的 Bug 与重构记录 (Debugging & Refactoring Log)

我们在代码审查过程中发现并修复了以下关键问题：

#### Bug 1: 信号量与屏障的 Key 冲突问题
-   **现象**: 两个进程分别调用 `semaphore_init(100, 1)`，结果各自拿到了不同的信号量 ID，导致互斥失效。
-   **解决**: 引入了 Key 检查机制，强制执行 "Get or Create" 语义。同时明确了对于 Barrier，如果 Key 已存在，后来的初始化请求中传入的 `goal` 参数将被忽略，以先创建者的配置为准。

#### Bug 2: 条件变量唤醒后的“裸奔”风险
-   **现象**: 用户程序 `while(cond) wait()` 结构中，`wait` 返回后进程处于无锁状态，导致随后的条件判断和数据修改发生竞争。
-   **解决**: 在内核态 `do_condition_wait` 的 `do_scheduler()` 之后强制插入 `do_mutex_lock_acquire()`。

#### Bug 3: Mailbox 逻辑死锁与溢出风险
-   **现象**:
    1.  若使用 `int` (有符号数) 作为游标，长期运行后游标变为负数，导致数组下标越界（Kernel Panic）。
    2.  在判断“满”或“空”时，使用 `wcur + len > rcur + size` 这种绝对值比较，在溢出回绕时逻辑会失效，导致进程意外永久阻塞。
-   **解决**:
    -   将游标类型全部改为 `unsigned int`。
    -   重构判断逻辑为“差值比较” (`wcur - rcur`)，消除了逻辑死锁隐患。
    -   增加了对 `msg_length > MAX_MBOX_LENGTH` 的边界检查，防止因用户传入非法长度导致的死锁。

#### Refactoring: 代码规范化
-   **优化**: 将 Mailbox 的内存拷贝逻辑从 `do_mbox_send/recv` 中剥离，封装为 `circular_buffer_copy`。
-   **优化**: 使用 `const void *` 修饰发送源数据，增强了函数的接口安全性。
-   **优化**: 引入 `typedef unsigned int uint32_t` 提升代码跨平台兼容性。
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

## Task 4: CPU 亲和性与动态绑核 (CPU Affinity)

### 1. 设计与实现思路

本任务的目标是实现 CPU 亲和性（Affinity），即控制进程在特定的 CPU 核上运行。我们采用 **掩码 (Mask)** 机制，`mask` 的每一位对应一个 CPU 核。

#### 1.1 内核数据结构与调度器
-   **PCB 修改**: 在 `pcb_t` 中增加 `int mask` 字段。
-   **调度算法 (`seek_ready_node`)**:
    -   修改了任务查找逻辑。调度器不再仅仅获取队列头部的任务，而是遍历 `ready_queue`。
    -   判断条件：`pcb->status == TASK_READY && (pcb->mask & (1 << current_cpu))`。
    -   只有满足当前核掩码要求的任务才会被调度运行，否则跳过。
-   **Mask 继承策略**:
    -   修改 `do_exec`，新创建的进程默认继承父进程 (`current_running`) 的 `mask`。
    -   这保证了由 Shell 启动的程序能继承 Shell 的亲和性设置。

#### 1.2 系统调用与 Shell 命令
-   **新增系统调用**: `sys_taskset(pid_t pid, int mask)`，对应内核函数 `do_taskset`，用于动态修改指定进程的 mask。
-   **Shell `taskset` 命令**:
    -   **启动模式** (`taskset mask cmd`): 利用 Shell 自身变身机制。Shell 先修改自己的 mask，执行 `exec`（子进程继承 mask），然后 Shell 恢复原来的 mask。
    -   **修改模式** (`taskset -p mask pid`): 直接调用 `sys_taskset` 修改目标进程。

---

### 2. 遇到的 Bug 与解决方案 (Debugging Log)

Task 4 的调试过程揭示了底层 ABI 和调度策略的两个关键问题：

#### Bug 1: `-O2` 优化下的内存对齐崩溃 (Alignment Fault)
-   **现象**: 在 `-O0` 下运行正常，但在 `-O2` 优化下，`affinity` 测试程序输出乱码或崩溃，且观察到内存地址偏移异常。
-   **原因**:
    -   SD 卡中程序的 `.text` 段起始偏移量为 140 字节 ($140 \% 8 \neq 0$)。
    -   旧的 Loader 直接返回 `BASE + 偏移` 的地址。
    -   在 `-O2` 优化下，编译器生成了 `ld` (64位加载) 指令来操作 `uint64_t` 数据。RISC-V 硬件/QEMU 要求 `ld` 指令访问的地址必须 **8字节对齐**。
    -   由于 140 不是 8 的倍数，导致非对齐访问 (Misaligned Access)，读取数据错误。
-   **解决**: 重写 `load_task_img`。不再直接返回偏移地址，而是使用 `memcpy` 将代码搬运到页对齐（Page Aligned，自然也是 8字节对齐）的内存基地址处。保证了所有指令和数据的地址对齐满足编译器假设。

#### Bug 2: 多核测试无加速效果 (Mask 继承链问题)
-   **现象**: 运行 `multicore` 测试，虽然双核均已启动，但加速比始终为 1.0 (单核性能)。
-   **原因**:
    -   内核初始化时，`pid0_pcb` (Idle Task) 的 `mask` 未显式初始化（默认为 0 或 1）。
    -   `shell` 由 `pid0` 启动，继承了 `mask=1` (仅主核)。
    -   `multicore` 由 `shell` 启动，继续继承 `mask=1`。
    -   结果导致 `multicore` 创建的所有计算子任务都被强制锁定在主核运行，从核处于空闲状态。
-   **解决**: 在 `sched.c` 初始化阶段，显式将 `pid0_pcb` 和 `s_pid0_pcb` 的 `mask` 设置为 `0x3` (允许所有核运行)。

---

## Task 5: 多核 Mailbox 防死锁 & 线程机制

### 1. 线程支持与系统调用

- **PCB 扩展**：为 `pcb_t` 增加 `tgid`、`thread_ret` 等字段，用于区分“进程 ID”(tgid) 与“线程 ID”(pid)。
- **内核线程栈初始化**：在 `init/main.c` 新增 `init_thread_stack`，复用现有上下文切换逻辑。
- **系统调用**：新增 `sys_thread_create/exit/join`，内核侧提供 `do_thread_create/exit/join`，并在 syscall 表与 TinyLibC 中注册。
- **调试辅助**：`ps` 命令现在会区分 `PROC` 与 `THRD`，显示 `PROC:<tgid>` 与 `TID:<pid>`，便于观察多线程任务的运行状态。

### 2. Mailbox 死锁复现与修复

- **复现程序 (`mbox_deadlock`)**：控制进程预填两个邮箱，两个 worker 分别执行“先收后发”但资源请求顺序相反，必然卡在 `sys_mbox_send`，验证死锁存在。
- **修复程序 (`mbox_thr_demo`)**：
  - 每个 worker 创建两个线程：`receiver_thread` 专管 `sys_mbox_recv`，`sender_thread` 专管 `sys_mbox_send`，两个线程通过共享缓冲 `data_ready` 交替工作，每次循环释放一个邮箱资源再申请另一个，破坏“占有且等待”条件。
  - Demo 仅运行固定轮数（默认 5 次），两个线程自然退出，主线程 `sys_thread_join` 后 `sys_exit`，控制进程等待两侧 worker 完成后打印“Demo finished”。
  - 运行效果：`exec mbox_thr_demo &` 后可以看到 `[Fix-A/B] send/recv #n` 连续输出，`ps` 中只有极少线程在 RUNNING/READY，其余均自动退出，说明死锁解除且任务可收尾。

### 3. 关键注意事项

- **加载隔离**：移除 `kernel/loader/loader.c` 中的 `is_load[]` 缓存，每次 `exec` 都重新搬运 ELF，保证不同进程的 `.data/.bss` 彼此隔离，线程状态不会相互覆盖。
- **兼容旧测试**：线程实现复用原有 PCB/调度基础设施，对 `mbox_client/server`、`multicore` 等测试无侵入影响；若需要查看线程分布，可使用更新后的 `ps`。

### 4. 测试步骤

1. `exec mbox_deadlock &`：观察两个 worker 永久 BLOCKED，作为失败基准。
2. `exec mbox_thr_demo &`：观察 send/recv 线程交替输出，待日志提示完成后 `ps` 仅余 shell，证明线程版 demo 成功退出。
3. `ps` 命令对比：可以直接看到 `PROC` 与 `THRD` 的区分，确认线程确实继承了父进程的 `tgid`。

---

