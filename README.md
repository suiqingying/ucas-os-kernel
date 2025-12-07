# Project 4: 内存管理 (Memory Management)

## 1. 项目概述 (Overall Goal)

本项目旨在为我们的操作系统引入核心的**虚拟内存（Virtual Memory）**机制。在此之前，所有进程都运行在同一个物理地址空间中，这带来了严重的安全隐患和管理难题。通过实现虚拟内存，我们将为每个进程提供一个独立的、受保护的、从零开始的虚拟地址空间，实现以下核心目标：

- **内存隔离**: 防止进程间非法访问内存，保障系统稳定性和安全性。
- **动态加载**: 允许用户程序被加载到物理内存的任意位置，而无需在编译时硬编码地址。
- **高级内存功能**: 为后续实现按需调页、页面换出、写时复制等高级功能奠定基础。

我们将采用 RISC-V 的 **Sv39** 分页模式，它使用**三级页表**将 39 位的虚拟地址转换为 56 位的物理地址。

---

## 2. 任务分解 (Task Breakdown)

### 2.1 Task 1: 启用虚拟内存并加载用户程序 (S-Core)

**核心目标**: 搭建虚拟内存的基础框架，让内核和用户程序都能在虚拟地址下正确运行。

*   **步骤 1: 理解 Sv39 虚拟内存机制**
    *   **地址格式**: 学习 Sv39 模式下的 39 位虚拟地址（VA）和 56 位物理地址（PA）的结构。
        *   VA: `VPN[2]` (9 bits) | `VPN[1]` (9 bits) | `VPN[0]` (9 bits) | `Page Offset` (12 bits)
        *   PA: `PPN[2]` (26 bits) | `PPN[1]` (9 bits) | `PPN[0]` (9 bits) | `Page Offset` (12 bits)
    *   **页表项 (PTE)**: 理解 64 位 PTE 的格式，特别是 `V, R, W, X, U` 等关键标志位的作用。
    *   **地址翻译过程**: 掌握 CPU 如何利用 `satp` 寄存器指向的页表基地址，通过三级页表（L2, L1, L0）将虚拟页号（VPN）翻译成物理页号（PPN）。
    *   **TLB**: 理解快表（TLB）作为地址翻译缓存的角色，以及 `sfence.vma` 指令的用途。

*   **步骤 2: 实现内核态的虚拟内存映射**
    *   **创建内核页表**: 在 `boot_kernel_map` 中为内核创建一个初始的**恒等映射（Identity Mapping）**或**线性映射（Linear Mapping）**。这意味着内核的虚拟地址和物理地址有固定的偏移关系（例如 `vaddr = paddr + offset`）。
    *   **映射关键区域**: 必须将内核代码段、数据段、栈、以及所有硬件设备（如 UART、PLIC）的物理地址映射到内核的虚拟地址空间中。
    *   **启用分页**: 在完成页表建立后，通过 `set_satp` 函数将页表基地址写入 `satp` 寄存器，并设置模式为 `SATP_MODE_SV39`，正式开启虚拟内存。
    *   **验证**: 确保开启分页后，内核依然能够通过虚拟地址正常运行（例如，`printk` 能够正常打印）。

*   **步骤 3: 为用户进程创建独立的地址空间**
    *   **修改加载器 (`load_task_img`)**:
        *   为每个新进程分配一个根页目录（L2 Page Table）。
        *   在加载 ELF 文件时，不再直接写入物理地址，而是为程序的 `.text`, `.data` 等段分配物理页框。
        *   在用户进程的页表中，建立从用户虚拟地址（如 `0x10000`）到新分配的物理页框的映射。
        *   为用户进程分配栈空间，并在其页表中建立对应的虚实映射。
    *   **修改上下文切换**: 在 `switch_to` 中，切换 `satp` 寄存器指向新进程的页表基地址，并刷新 TLB (`sfence.vma`)，以完成地址空间的切换。

---

### 2.2 Task 2: 实现按需调页 (On-demand Paging) (A-Core)

**核心目标**: 优化内存使用，只有在进程实际访问一个页面时才为其分配物理内存。

*   **步骤 1: 实现缺页异常处理 (Page Fault Handler)**
    *   在 `trap_handler` 中，识别由 `scause` 寄存器指示的 `Store page fault`, `Load page fault`, `Instruction page fault` 异常。
    *   从 `stval` 寄存器中获取导致缺页的虚拟地址。
    *   编写 `do_page_fault` 函数，负责处理缺页逻辑。

*   **步骤 2: 实现按需分配**
    *   修改 `do_exec`，在加载用户程序时，只建立页表结构，但**不立即分配物理页框**。PTE 的 `V` (Valid) 位保持为 0。
    *   在 `do_page_fault` 中，当检测到缺页是由于访问一个合法的、但尚未分配物理内存的区域时（例如，通过检查进程的虚拟内存区域 VMA 来判断），执行以下操作：
        1.  分配一个新的物理页框。
        2.  更新导致缺页的地址所对应的 PTE，填入新页框的 PPN，并设置 `V, R, W, X, U` 等正确的权限位。
        3.  刷新 TLB。
        4.  从异常处理中返回，让指令重新执行。

---

### 2.3 Task 3: 实现页面换出与换入 (Page Swap) (A-Core)

**核心目标**: 当物理内存耗尽时，能将不常用的页面临时存放到磁盘（SD卡）上，从而支持运行比物理内存更大的程序。

*   **步骤 1: 设计磁盘交换区 (Swap Area)**
    *   在 SD 卡上预留一块连续的空间作为交换区。
    *   实现一个简单的分配器来管理交换区中的空闲槽位。

*   **步骤 2: 实现页面换出 (Page Out)**
    *   当 `alloc_page` 发现物理内存不足时，触发页面替换算法（如 FIFO 或 Clock）。
    *   **选择牺牲页**: 找到一个要被换出的物理页框。
    *   **写回磁盘**: 将该页框的内容写入到 SD 卡交换区的一个空闲槽位。
    *   **更新页表**: 找到所有映射到这个物理页框的 PTE，将它们标记为**无效**（`V=0`），并在 PTE 的软件保留位中记录它在交换区的位置。
    *   **刷新 TLB**: 确保对该页面的后续访问会触发缺页异常。

*   **步骤 3: 实现页面换入 (Page In)**
    *   修改 `do_page_fault`。当缺页发生时，检查 PTE 的软件保留位。
    *   如果 PTE 表明该页已被换出到磁盘，则：
        1.  分配一个新的物理页框。
        2.  从 SD 卡的交换区将页面内容读回这个新页框。
        3.  更新 PTE，指向新的物理页框，并设置 `V=1`。
        4.  释放交换区中的槽位。

---

### 2.4 Task 4: 查看可用内存 (C-Core)

**核心目标**: 实现一个系统调用，让用户程序可以查询当前系统的剩余物理内存。

*   **步骤 1: 实现 `sys_get_mem`**
    *   在内核中维护一个全局变量，记录当前空闲物理页框的数量。
    *   实现一个新的系统调用，读取这个变量并乘以页大小，返回给用户。

---

### 2.5 Task 5: 内存页管道 (Memory Page Pipe) (C-Core)

**核心目标**: 实现一种高效的进程间通信机制，通过直接传递页面的所有权（重映射页表）来避免数据拷贝。

*   **步骤 1: 设计页管道 API**
    *   实现 `pipe_read(char *buf, int len)` 和 `pipe_write(char *buf, int len)` 系统调用。

*   **步骤 2: 实现零拷贝传递**
    *   **写操作**: 当进程 A 调用 `pipe_write` 时，内核找到 `buf` 所在的物理页框。
    *   **读操作**: 当进程 B 调用 `pipe_read` 时，内核不分配新内存，而是直接将进程 B 页表中 `buf` 对应的虚拟地址，映射到进程 A 之前写入的那个物理页框上。
    *   **所有权转移**: 为了安全，可以将进程 A 对该页面的映射解除或设置为只读，实现页面的所有权从 A 转移到 B。



## 3. Task 1 实验总结

### 3.1 实验完成情况
本阶段完成了 **S-core / A-core** 的核心要求：
1.  **启用虚拟内存**：成功实现了从物理地址模式到 Sv39 虚拟地址模式的切换，内核成功运行在 `0xffffffc0...` 高地址空间。
2.  **内核页表管理**：实现了 `alloc_page_helper`，支持三级页表的动态分配与映射。
3.  **用户进程加载**：重写了 `loader`，实现了基于页面的 ELF 加载（Page-based Loading），支持从 SD 卡加载用户程序到独立的虚拟地址空间。
4.  **系统调用支持**：实现了 `exec`（创建新地址空间）、`kill`（终止进程）、`getpid` 等系统调用。
5.  **Shell 运行**：Shell 成功启动，支持后台运行程序 (`&`) 和进程管理。

### 3.2 关键实现细节

#### 3.2.1 内存布局与启动
*   **物理搬运**：在 `bootblock.S` 中将内核搬运至 `0x50202000`。
*   **虚拟映射**：在 `head.S` 中初始化虚拟地址栈指针。在 `main.c` 初始化后，调用 `cancel_identity_mapping` 取消了 `0x50000000` 低地址段的临时恒等映射，确保内核安全。
*   **地址转换**：严格区分内核虚拟地址 (KVA) 和物理地址 (PA)，实现了 `kva2pa` 和 `pa2kva` 宏及辅助函数。

#### 3.2.2 内存管理 (`mm.c`)
*   **`alloc_page_helper`**：实现了虚拟地址到物理地址的映射逻辑。
    *   **L2/L1 页表**：分配页表页，设置 `_PAGE_PRESENT`（移除 `_PAGE_USER` 以符合规范）。
    *   **L0 叶子页**：分配物理页，设置完整的 `R/W/X/U/A/D` 权限。
*   **`allocPage`**：修改为从 `FREEMEM_KERNEL` (`0xffffffc052002000`) 开始分配内核虚拟页。

#### 3.2.3 程序加载器 (`loader.c`)
*   **结构体对齐**：修复了 `task_info_t` 在宿主机 (`createimage`) 和开发板 (`kernel`) 之间 `int` vs `uint64_t` 的定义不一致问题。
*   **按页加载**：不再连续读取大块内存，而是解析段信息，按 4KB 粒度分配物理页并建立映射。
*   **非对齐处理**：引入**临时缓冲区**，解决了 SD 卡只能按扇区读取导致的非 4KB 对齐（Instruction Misaligned）问题。
*   **I-Cache 一致性**：在加载完成后执行 `fence.i`，确保 CPU 指令缓存读取到最新的代码。

#### 3.2.4 进程调度与上下文
*   **`do_exec`**：为新进程分配独立的页目录 (`pgdir`)，复制内核高地址映射，初始化用户栈（将参数拷贝到物理页对应的 KVA 中）。
*   **`do_scheduler`**：在进程切换时，通过 `set_satp` 切换页表，并执行 `sfence.vma` 刷新 TLB。
*   **用户访问权限**：在 `init_pcb_stack` 中设置 `sstatus` 的 **`SUM`** 位，允许内核在系统调用（如 `sys_write`）期间读取用户态的字符串指针。

### 3.3 遇到的主要问题与解决方案 (Troubleshooting)

在实验过程中，我们遇到并解决了一系列严重的 Bug，这也是本次实验最大的收获：

| 问题现象 | 原因分析 | 解决方案 |
| :--- | :--- | :--- |
| **Boot 阶段崩溃** | `app_info_ptr` 使用了 `ptr_t*` (64位) 读取 32位数据，导致读取大小溢出。 | 修改为 `int32_t*` 读取，修正地址计算。 |
| **Loader 读出乱码** | `createimage` 与 Kernel 的 `task_info_t` 结构体大小不一致 (32B vs 40B)，导致偏移错位。 | 统一将 `task_size`, `p_memsz` 定义为 `uint64_t` 并重新编译工具。 |
| **Instruction Page Fault (0x10000)** | 1. 刚加载的代码未刷新到指令缓存。<br>2. 中间页表项错误设置了 User 权限位。 | 1. 添加 `fence.i` 指令。<br>2. 移除 L1/L2 页表项的 `_PAGE_USER` 标志。 |
| **Shell 无输出 (Silent)** | 内核开启虚存后，S 模式默认禁止访问 U 模式页面，导致 `sys_write` 读取 buffer 失败。 | 在 `sstatus` 中开启 `SUM` (Supervisor User Memory access) 位。 |
| **kill 进程后系统卡死** | `do_kill` 和 `pcb_release` 对同一个链表节点进行了两次删除 (Double Free)。 | 移除 `do_kill` 中的手动删除代码，统一由 `pcb_release` 处理。 |

### 3.4 后续工作 (Outlook)

虽然 Task 1 已完成，但为了支持 Task 2/3 (缺页与换页)，后续还需完善：
1.  **完整的内存回收**：目前 `freePage` 为空，`free_pgdir` 虽然逻辑写好但未启用（防止不稳定）。需要在实现空闲链表分配器后启用。
2.  **缺页异常处理**：目前所有页面都是 `exec` 时静态分配的 (Pre-allocation)。Task 2 需要将其改为按需分配 (On-demand)。

### 3.5 多核与并发问题 (SMP & Concurrency)

本次实验在双核（S-Core / C-Core）环境下遇到并解决了两个极具挑战性的并发 Bug，深刻理解了多核操作系统的初始化流程。

#### 3.5.1 启动时的竞争条件 (Boot Race Condition)
*   **现象**：双核启动时，从核经常卡死或触发 Instruction Page Fault。
*   **原因**：
    *   主核 (Core 0) 负责建立内核页表 (`setup_vm`)。
    *   从核 (Core 1) 启动较快，在主核还没建好页表时就尝试开启虚存 (`enable_vm`)，导致访问非法地址。
*   **解决方案**：
    *   在 `boot.c` 中引入同步变量 `pgtable_init_done`。
    *   **关键点**：该变量必须强制放入 `.data` 段（避免 BSS 段未清零导致的随机值），且主核写完后需执行 `fence` 指令。从核自旋等待该变量置位后才开启虚存。

#### 3.5.2 空闲进程初始化不全 (Uninitialized Idle Process)
*   **现象**：
    *   可以正常在 Core 1 运行程序 (`taskset 0x2 fly`)。
    *   **不对称性**：如果 Core 1 空闲，直接在 Core 0 启动程序可能导致 Core 1 挂死。
    *   **致命 Bug**：当 `kill` 掉 Core 1 上的进程时，双核同时死锁。
*   **原因**：
    *   调度器在 `RunQueue` 为空时会调度空闲进程 (`s_pid0_pcb`)。
    *   代码中未初始化 `s_pid0_pcb.pgdir`。
    *   当进程被 Kill，调度器强制切换回 `s_pid0`，将 `satp` 设置为 0，导致 Core 1 瞬间崩溃并持有自旋锁，进而导致 Core 0 等锁卡死。
*   **解决方案**：
    *   在 `init_pcb` 中，显式初始化 `s_pid0_pcb.pgdir = pa2kva(PGDIR_PA)`。
    *   确保所有 PCB（包括 idle）都有合法的页表和上下文。

---

### 3.6 遇到的主要问题与解决方案 (Troubleshooting - 更新版)

| 问题现象 | 原因分析 | 解决方案 |
| :--- | :--- | :--- |
| **Boot 阶段崩溃** | `app_info_ptr` 使用了 `ptr_t*` (64位) 读取 32位数据，导致读取大小溢出。 | 修改为 `int32_t*` 读取，修正地址计算。 |
| **Loader 读出乱码** | `createimage` 与 Kernel 的 `task_info_t` 结构体大小不一致 (32B vs 40B)，导致偏移错位。 | 统一将 `task_size`, `p_memsz` 定义为 `uint64_t` 并重新编译工具。 |
| **页表权限错误** | L1/L2 页表项错误设置了 User 权限位，违反 RISC-V 规范。 | 移除 L1/L2 页表项的 `_PAGE_USER` 标志。 |
| **Instruction Page Fault (0x10000)** | 1. 刚加载的代码未刷新到指令缓存。<br>2. 中间页表项错误设置了 User 权限位。 | 1. 添加 `fence.i` 指令。<br>2. 移除 L1/L2 页表项的 `_PAGE_USER` 标志。 |
| **Shell 无输出 (Silent)** | 内核开启虚存后，S 模式默认禁止访问 U 模式页面，导致 `sys_write` 读取 buffer 失败。 | 在 `sstatus` 中开启 `SUM` (Supervisor User Memory access) 位。 |
| **taskset 0x1 fly 卡死 (不对称)** | Core 0 的 FPU 或 SUM 状态位未初始化，而 Core 1 恰好由 BIOS 初始化了。 | 在 `init_pcb_stack` 中强制设置 `sstatus` = `SUM | FS | SPIE`。 |
| **kill 进程后双核死锁** | Core 1 切换回空闲进程时，因 `s_pid0.pgdir` 为 0 导致崩溃，且持有屏幕锁导致 Core 0 等待。 | 初始化 `s_pid0_pcb.pgdir` 为内核页表地址。 |
| **do_kill 的逻辑错误** | `do_kill` 和 `pcb_release` 对同一个链表节点进行了两次删除 (Double Free)。 | 移除 `do_kill` 中的手动删除代码，统一由 `pcb_release` 处理。 (在一次临时修改中引入这个错误)|
| **pgtable_init_done 默认值随机** | 该变量放在 BSS 段，未初始化前可能为非 0，导致从核误以为页表已初始化。 | 强制将 `pgtable_init_done` 放入 `.data` 段，并在主核写入后执行 `fence`。 |

### 3.7 boot/kernel 启动流程分析
在 RISC-V 架构下，操作系统内核的启动流程涉及多个阶段，从最初的引导代码到最终进入内核主函数。以下是对 `arch/riscv/kernel/start.S` 和 `arch/riscv/boot/bootblock.S` 中启动流程的详细分析：
#### 3.7.1 引导代码 (bootblock.S)
*   **位置**: `arch/riscv/boot/bootblock.S` 是
    引导代码的主要文件，负责在系统上电或复位后执行的第一段代码。
*   **功能**:
    1. **初始化堆栈指针**: 设置初始堆栈指针，确保后续函数调用有正确的栈空间。
    2. **加载内核**: 将内核映像从存储设备（如 SD 卡）加载到内存的指定位置。
    3. **设置内存映射**: 配置初始的内存映射，确保内核代码和数据可以被正确访问。
    4. **跳转到内核入口**: 最后，跳转到内核的启动入口点，通常是 `boot_kernel` 函数。
#### 3.7.2 内核启动代码 (start.S)
*   **位置**: `arch/riscv/kernel/start.S` 包含内核启动的汇编代码。
*   **功能**:
    1. **设置堆栈指针**: 在内核空间设置堆栈指针，通常指向内核栈的顶部。
    2. **获取 Hart ID**: 读取当前处理器的 Hart ID（多核处理器中的核编号），以便内核可以根据不同的核执行不同的初始化逻辑。
    3. **调用内核启动函数**: 最后，调用 `boot_kernel` 函数，进入内核的 boot.c 主函数，开始内核的初始化过程。
#### 3.7.3 boot_kernel 函数
*   **位置**: `arch/riscv/kernel/boot.c` 中的 `boot_kernel` 函数是内核启动的主要入口点。
*   **功能**:
    1. **初始化页表**: 设置虚拟内存管理，建立内核的页表映射。
    2. **跳转到 head.S**: 继续执行 `arch/riscv/kernel/head.S` 中的代码，完成更高级别的内核初始化。
#### 3.7.4 head.S
*   **位置**: `arch/riscv/kernel/head.S` 负责内核的早期初始化。
*   **功能**:
    1. **设置内核栈**: 初始化内核栈指针。
    2. **调用 main 函数**: 最终跳转到 `main` 函数，开始内核的主要初始化工作，如设备初始化、进程调度等。
#### 3.7.5 启动阶段分解
整个启动流程从引导代码开始，经过内核启动代码，最终进入内核主函数。每个阶段都有其特定的职责，确保系统能够正确初始化并运行内核。

在引入虚拟内存（Project 4）之后，操作系统的启动流程变得比之前复杂，主要原因是涉及到了**物理地址到虚拟地址的切换**。

整个启动顺序如下：

**`bootblock` $\to$ `start.S` $\to$ `boot.c` $\to$ `head.S` $\to$ `main.c`**

下面是详细的阶段解析：

##### 3.7.5.1 第一阶段：搬运工 (`bootblock.S`)
*   **运行环境**：**物理地址** (MMU 关闭)。
*   **任务**：BIOS 启动后首先运行 `bootblock`。它的唯一任务是从 SD 卡把巨大的内核镜像（Kernel Image）搬运到物理内存的指定位置（例如 `0x50202000`）。
*   **下一步**：搬运完成后，它直接跳转到内核在物理内存中的起始位置。

##### 3.7.5.2 第二阶段：物理地址入口 (`start.S`)
*   **什么时候执行？** `bootblock` 跳转后立即执行。
*   **运行环境**：**物理地址** (MMU 关闭)。
*   **为什么需要它？**
    *   虽然内核代码在编译时被链接到了高地址（虚拟地址 `0xffffffc0...`），但此刻 CPU 还在用物理地址跑。
    *   如果直接运行 C 代码或访问全局变量，CPU 会尝试访问虚拟地址，导致崩溃。
    *   所以需要这一小段汇编，它**不依赖绝对地址**（使用相对寻址），负责建立一个临时的物理栈，以便能运行后续的 C 代码。
*   **下一步**：调用 C 函数 `boot_kernel`。

##### 3.7.5.3 第三阶段：开启虚拟内存 (`boot.c`)
*   **什么时候执行？** 被 `start.S` 调用。
*   **运行环境**：**物理地址 $\to$ 虚拟地址** 的切换点。
*   **任务**：
    1.  `setup_vm()`: (主核) 建立页表。映射关系包括：
        *   虚拟高地址 $\to$ 物理地址 (供未来内核使用)。
        *   **物理地址恒等映射** (例如 `0x50200000` 映射到 `0x50200000`)。这是为了保证开启 MMU 的瞬间，PC 指针还能取到指令，不会立刻崩掉。
    2.  `enable_vm()`: 写入 `satp` 寄存器，**开启 MMU**。
*   **关键跳转**：
    *   这是最魔术的一步。
    *   代码计算出 `head.S` 中 `_start` 标号对应的**内核虚拟地址**。
    *   使用跳转指令，强制将 PC 指针从物理地址（低地址）飞跃到虚拟地址（高地址）。

##### 3.7.5.4 第四阶段：虚拟地址入口 (`head.S`)
*   **什么时候执行？** 在 `boot.c` 完成虚拟内存开启并跳转后执行。
*   **运行环境**：**内核虚拟地址** (MMU 开启)。
*   **为什么需要它？**
    *   现在我们终于处于“正统”的内核环境了。
    *   它负责设置**真正的内核栈**（这个栈位于高虚拟地址）。
    *   它负责**清空 BSS 段**（初始化全局变量为 0）。之前在 `start.S` 时不能清空 BSS，因为那时候通过虚拟地址访问不到 BSS 段。
*   **下一步**：跳转到 `main()`。

##### 3.7.5.5 第五阶段：主逻辑 (`main.c`)
*   **什么时候执行？** 被 `head.S` 调用。
*   **运行环境**：完全的虚拟内存环境。
*   **任务**：初始化 PCB、调度器、加载 Shell 等。

---

#### 3.7.6 总结图示

```text
1. [bootblock.S] (物理地址)
   | 搬运内核到内存 0x50202000
   | 跳转 -> 0x50202000
   v
2. [start.S] (_boot) (物理地址)
   | 位于内核二进制的最开头
   | 设置临时物理栈
   | 调用 boot_kernel
   v
3. [boot.c] (boot_kernel) (物理地址 -> 虚拟地址)
   | 建立页表 (setup_vm)
   | 开启 MMU (enable_vm)
   | 计算 _start 的虚拟地址
   | 跳转 -> 0xffffffc0....... (_start)
   v
4. [head.S] (_start) (虚拟地址)
   | 设置正式的内核虚拟栈
   | 清空 BSS 段
   | 跳转 -> main
   v
5. [main.c] (虚拟地址)
   | OS 正式启动
```

#### 3.7.7 这个顺序是谁决定的？

1.  **`bootblock` $\to$ `start.S`**：由 `bootblock` 的代码决定（它硬编码了跳转地址），同时由 **链接脚本 (`riscv.lds`)** 决定，链接脚本保证了 `start.S` 永远位于内核镜像文件的第一个字节。
2.  **`start.S` $\to$ `boot.c`**：由汇编指令 `call boot_kernel` 决定。
3.  **`boot.c` $\to$ `head.S`**：由 C 代码中的函数指针跳转决定 `((kernel_entry_t)pa2kva(_start))`。
4.  **`head.S` $\to$ `main.c`**：由汇编指令 `j main` 决定。

### 3.8 对于 fence 指令的理解

fence 指令是 RISC-V 架构中的一个内存屏障指令，用于确保指令执行的顺序和内存访问的可见性。

#### 3.8.1 fence 指令的真正威力：双向约束

您认为 fence 只能阻止它之后的指令被提前。但实际上，fence 指令（在 RISC-V 中，默认的 fence 指令具有强大的排序能力）像一个双向的"栅栏"，它同时约束了它之前和之后的内存操作：

- **向前的约束 (Release 语义)**：它强制要求，所有在 fence 指令之前的内存写操作（比如 `setup_vm()` 中对页表的无数次写入），都必须在 fence 指令本身完成之前，全部完成并且其结果对其他核心可见。
- **向后的约束 (Acquire 语义)**：它禁止所有在 fence 指令之后的内存读写操作，被乱序执行到 fence 指令之前。

然而，这里的关键在于：CPU 的写缓冲区（Store Buffer）本身，对于来自同一个核心的写操作，是严格遵循程序顺序（Program Order）的，它是一个 FIFO（先进先出）队列。

#### 3.8.2 CPU 的"乱"与"不乱"

让我们来彻底澄清这个最容易混淆的概念。

CPU 的乱序执行，主要体现在它会打乱没有相互依赖关系的不同类型指令的执行顺序（比如计算指令、读取指令、跳转指令）。但是，对于同一个核心发出的多个写操作，CPU 为了保证自身逻辑的正确性，必须遵守一个基本原则：

**单核一致性（Single-Core Coherence）**：一个 CPU 核心自己看到的自己的写操作，其效果必须和程序编写的顺序一致。

为了实现这一点，写缓冲区（Store Buffer）的设计就是 FIFO 的。

### 3.9 启动阶段的竞态条件（Boot Race Condition）

TL;DR：如果主核（Hart 0）在从核（Hart 1）还未完成启动前就取消了低地址的恒等映射，从核在开启 MMU 后可能会因为取指失败而崩溃。

问题描述：
- 在多核启动时，Hart 0 负责建立内核页表并开启虚拟内存；为了安全，Hart 0 可能会随后取消低地址的临时恒等映射（identity mapping）。
- 若 Hart 1 较慢、正在物理地址（low PA）区域执行 boot.c 或 head.S 的代码片段，且在 Hart 0 取消映射后才写入 `satp` 开启虚存，那么 Hart 1 的 PC 仍然是物理地址，MMU 会在页表中查找该物理地址对应的映射；如果映射已被清除，Hart 1 会触发 Instruction Page Fault。

典型时间线：
1. T0: Hart 0 与 Hart 1 同时上电。
2. T1: Hart 0 快速完成 `setup_vm()` 并跳转到高地址执行 `main()`。
3. T2: Hart 0 调用 `cancel_identity_mapping()`，移除低地址的恒等映射。
4. T3: Hart 1 较慢，执行到 `enable_vm()` 并准备写入 `satp`。
5. T4: Hart 1 写入 `satp` 开启虚拟内存，但此时其 PC 仍指向物理地址（例：0x50201004）。
6. T5: Hart 1 取指时，MMU 根据页表查找物理地址对应的映射——此映射已在 T2 被清除。
7. Crash: Hart 1 触发缺页异常，系统挂起。

后果：
- 系统可能出现启动失败、从核卡死或整个系统不稳定的情况；调试难度高且重现不一定稳定。

缓解思路（可选方案）：
- 在页表完全建立并且所有核都确认可安全开启虚存前，不要取消低地址的恒等映射；也就是说，延迟 `cancel_identity_mapping()`。
- 使用跨核同步变量（例如 `pgtable_init_done`），并在主核写完后执行 `fence`，从核在看到该变量置位后再开启虚存；必须确保该变量位于 `.data`（而非 BSS），并配合适当的内存屏障。
- 在从核开启虚存前，临时使用物理地址到虚拟高地址的稳定映射，或为 boot 代码保留一个最小的、不会被取消的映射区域。
- 但是我确实没想明白具体怎么做到

### 3.10 用户栈布局 (User Stack Layout)
用户虚拟地址 (UVA)        内存内容说明
-----------------------------------------------------------------------
    0xf00010000  <------- 用户栈底 (Stack Base) / 物理页结束位置
         |
         |       [参数字符串区域]
         |       存放具体的参数内容 (包括 '\0')
         |       例如: "arg2\0"
         |       例如: "arg1\0"
         |       例如: "shell\0" (argv[0])
         |
         v
    0x........   <------- 对齐填充 (Padding)
         |       为了满足 16字节 (128-bit) 对齐要求，
         |       这里可能需要填充 0 到 15 个字节。
         |
    0x........   <------- argv 指针数组结束
         |
         |       [argv 指针数组区域]
         |       存放的是指向上面[参数字符串区域]的 用户虚拟地址。
         |       每个指针占用 8 字节 (64-bit系统)。
         |
         |       argv[2] (指向 "arg2" 的首地址)
         |       argv[1] (指向 "arg1" 的首地址)
         |       argv[0] (指向 "shell" 的首地址)
         |
    0x........   <------- 用户栈顶 (sp) / argv 数组起始位置
         |
         v
    (低地址方向)

### 3.11 关于 allock_page_helper 
给一个虚拟地址分配物理地址并且返回一个内核对应虚拟地址
但是本质上三者代指统一内容：
虚拟地址 <-> 物理地址 <-> 内核虚拟地址
- 虚拟地址 (VA)：进程或内核使用的地址空间，是经过 MMU 映射后的地址。
- 物理地址 (PA)：实际的物理内存地址，是硬件访问的地址。
- 内核虚拟地址 (KVA)：内核空间使用的虚拟地址，通常映射到物理地址的高地址区域。

### 3.12 Task 2 关键 Bug 修复：进程切换时必须切换页表

#### 3.12.1 问题现象

运行两个 `lock` 进程时，系统崩溃：
```
exception code: 2, Illegal instruction, epc 0, ra 0
```

#### 3.12.2 问题根因

在 `do_mutex_lock_acquire` 中，当进程因锁竞争失败而阻塞时，原代码**直接调用 `switch_to` 切换进程，但没有切换页表**：

```c
// 错误代码
do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
pcb_t *prior_running = current_running;
current_running = get_pcb_from_node(seek_ready_node());
switch_to(prior_running, current_running);  // ❌ 没有切换 satp！
```

**后果**：新进程使用旧进程的页表，虚拟地址映射到错误的物理地址，导致取指令失败。

#### 3.12.3 修复方案

**所有进程切换必须通过 `do_scheduler()` 完成**，因为只有 `do_scheduler()` 会正确切换页表：

```c
// 正确代码
do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
do_scheduler();  // ✅ 自动处理页表切换
```

#### 3.12.4 设计原则

任何导致进程切换的操作（锁阻塞、信号量、条件变量、sleep 等）都必须调用 `do_scheduler()`，不能手动调用 `switch_to()`。
## 4. Task 2: 按需调页实现与关键 Bug 修复

### 4.1 实现概述
本阶段完成了 Task 2 的核心功能：**按需调页（On-demand Paging）**，并在调试过程中发现并修复了多个与虚拟内存管理、进程切换和并发相关的严重 Bug。

### 4.2 缺页异常处理 (`handle_page_fault`)

#### 实现要点：
```c
void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause) {
    // 1. 对齐到页边界
    uintptr_t fault_va = stval & ~(PAGE_SIZE - 1);
    
    // 2. 建立映射（alloc_page_helper 会检查是否已存在）
    alloc_page_helper(fault_va, current_running->pgdir);
    
    // 3. 刷新 TLB（只刷新这一页即可）
    local_flush_tlb_page(fault_va);
}
```

**关键改进**：
- **地址对齐**：确保缺页地址对齐到 4KB 页边界
- **精确 TLB 刷新**：从 `local_flush_tlb_all()` 优化为 `local_flush_tlb_page()`，减少性能开销
- **幂等性**：`alloc_page_helper` 会检查页表项是否已存在，避免重复分配

### 4.3 内存分配优化 (`alloc_page_helper`)

#### 核心改进：
```c
uintptr_t alloc_page_helper(uintptr_t va, uintptr_t pgdir) {
    // 1. 入口地址对齐
    va = va & ~(PAGE_SIZE - 1);
    
    // ... 三级页表遍历与分配 ...
    
    // 2. 分配物理页并清零
    ptr_t final_page = allocPage(1);
    memset((void *)final_page, 0, PAGE_SIZE);  // 防止访问到脏数据
    
    // 3. 设置正确的权限位
    set_pfn(&pte[vpn0], kva2pa(final_page) >> NORMAL_PAGE_SHIFT);
    set_attribute(&pte[vpn0], _PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | 
                              _PAGE_EXEC | _PAGE_USER | _PAGE_ACCESSED | _PAGE_DIRTY);
    
    return final_page;
}
```

**关键修复**：
- **页面清零**：新分配的物理页必须清零，防止用户进程读取到内核或其他进程的残留数据（安全漏洞）
- **地址对齐**：确保所有虚拟地址都对齐到页边界
- **权限正确性**：叶子节点设置完整权限（R/W/X/U/A/D），中间节点只设置 V 位

### 4.4 用户栈预分配

#### 问题场景：
当用户程序参数较多或字符串较长时，参数拷贝操作可能跨越页边界：
```text
用户栈布局：
0xf00010000  <--- 栈底（第一页）
    |
    | argv 字符串可能跨页到这里
    |
0xf0000f000  <--- 第二页（未映射）
```

#### 解决方案：
```c
// 在 do_exec 中预分配 2 页用户栈
uintptr_t stack_page_kva = alloc_page_helper(user_stack_base - PAGE_SIZE, pgdir);
alloc_page_helper(user_stack_base - 2 * PAGE_SIZE, pgdir);  // 防止跨页访问
```

### 4.5 最关键的 Bug：进程切换时未切换页表

#### 问题根源：
在 `do_mutex_lock_acquire` 中，当进程因锁竞争失败而阻塞时，代码直接调用 `switch_to` 切换到下一个进程：

**原始错误代码**：
```c
void do_mutex_lock_acquire(int mlock_idx) {
    if (spin_lock_try_acquire(&mlocks[mlock_idx].lock)) {
        mlocks[mlock_idx].pid = current_running->pid;
        return;
    }
    // 获取锁失败
    do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
    pcb_t *prior_running = current_running;
    current_running = get_pcb_from_node(seek_ready_node());
    current_running->status = TASK_RUNNING;
    switch_to(prior_running, current_running);  // ❌ 没有切换页表！
}
```

#### 问题表现：
1. **现象**：运行单个 `rw` 测试正常，但运行两个 `lock` 进程时立即崩溃
2. **错误信息**：`exception code: 2, Illegal instruction, epc 0, ra 0`
3. **时序分析**：
   ```
   T1: lock进程1（pid=2）运行，使用页表 pgdir_2
   T2: lock进程2（pid=3）启动，尝试获取同一个锁
   T3: 进程3获取锁失败，在 do_mutex_lock_acquire 中被阻塞
   T4: 直接调用 switch_to 切换回进程2
   T5: ❌ 问题：satp 仍然指向进程3的页表 pgdir_3
   T6: 进程2 尝试执行代码，虚拟地址 0x10000 通过 pgdir_3 映射到错误的物理地址
   T7: CPU 取指令失败，触发非法指令异常（epc=0 表示页表项无效）
   ```

#### 正确修复：
```c
void do_mutex_lock_acquire(int mlock_idx) {
    if (spin_lock_try_acquire(&mlocks[mlock_idx].lock)) {
        mlocks[mlock_idx].pid = current_running->pid;
        return;
    }
    // 获取锁失败，阻塞当前进程并调度
    do_block(&current_running->list, &mlocks[mlock_idx].block_queue);
    do_scheduler();  // ✅ 使用调度器，确保页表正确切换
}
```

`do_scheduler` 中的页表切换逻辑：
```c
void do_scheduler(void) {
    // ... 选择下一个进程 ...
    
    // 切换页表
    uintptr_t pgdir_pa = kva2pa(current_running->pgdir);
    set_satp(SATP_MODE_SV39, 0, pgdir_pa >> NORMAL_PAGE_SHIFT);  // 切换 satp
    
    switch_to(prior_running, current_running);
}
```

### 4.6 调度器中的缓存刷新优化

#### 原始问题：
```c
set_satp(SATP_MODE_SV39, 0, pgdir_pa >> NORMAL_PAGE_SHIFT);
local_flush_icache_all();  // ❌ 不必要的指令缓存刷新
```

#### 问题分析：
- `local_flush_icache_all()` 会清空整个指令缓存，严重影响性能
- 页表切换不需要刷新指令缓存（代码本身没变，只是映射关系变了）
- `set_satp` 内部已经包含 `sfence.vma`（TLB 刷新），足够保证正确性

#### 修复：
```c
set_satp(SATP_MODE_SV39, 0, pgdir_pa >> NORMAL_PAGE_SHIFT);
// 移除 local_flush_icache_all()
```

**注意**：只有在加载新代码到内存时（如 `load_task_img` 完成后）才需要 `fence.i`。

### 4.7 问题总结与经验教训

| 问题类型 | 根本原因 | 解决方案 | 教训 |
|---------|---------|---------|------|
| **锁竞争时崩溃** | `do_mutex_lock_acquire` 中手动切换进程未切换页表 | 改为调用 `do_scheduler()` | 任何进程切换必须通过调度器，确保页表切换 |
| **随机非法指令** | 新分配的页面未清零，包含脏数据 | 在 `alloc_page_helper` 中添加 `memset` | 安全性：所有新页必须清零 |
| **参数传递失败** | 用户栈只分配 1 页，参数跨页时缺页 | 预分配 2 页用户栈 | 栈操作可能跨页，需预留空间 |
| **缺页处理慢** | 每次缺页刷新全部 TLB | 改为 `local_flush_tlb_page()` | 精确操作，减少不必要的开销 |
| **O2 优化异常** | 未对齐地址 + 未初始化内存暴露问题 | 地址对齐 + 页面清零 | O2 优化会改变执行顺序，暴露隐藏 Bug |

### 4.8 关键设计原则

#### 原则 1：进程切换的统一性
**所有导致进程切换的操作必须通过 `do_scheduler()` 完成**，包括：
- 时间片耗尽（定时器中断）
- 主动让出 CPU（`sys_yield`）
- 进程阻塞（锁、信号量、sleep）
- 进程退出或被杀死

**错误示例**（手动切换）：
```c
current_running = next_pcb;
switch_to(old, new);  // ❌ 缺少页表切换
```

**正确示例**（通过调度器）：
```c
do_block(&current_running->list, &wait_queue);
do_scheduler();  // ✅ 自动处理页表切换
```

#### 原则 2：内存安全性
- 所有新分配的页面必须清零（`memset`）
- 用户可访问的内存必须设置 `_PAGE_USER` 权限
- 中间页表项不能设置 `_PAGE_USER`（RISC-V 规范）

#### 原则 3：地址对齐
- 所有虚拟地址操作前必须对齐到页边界：`va & ~(PAGE_SIZE - 1)`
- 页表项中的 PPN 移位时必须正确：`>> NORMAL_PAGE_SHIFT`

### 4.9 验证与测试

#### 4.9.1 测试用例 1：基本内存读写（rw）
```bash
> root@UCAS_OS: exec rw 0x3343 0x7d84 0x913
0x3343, 127501189
0x7d84, 1025670572
0x913, 120064583
Success!
```
✅ 通过（O0 和 O2 均正常）

#### 4.9.2 测试用例 2：多进程锁竞争（lock）
```bash
> root@UCAS_OS: exec lock 7 &
Info: excute lock successfully, pid = 2
> root@UCAS_OS: exec lock 8 &
Info: excute lock successfully, pid = 3
> [TASK] Applying for a lock.
> [TASK] Has acquired lock and running.(0)
> [TASK] Has acquired lock and running.(1)
```
✅ 通过（修复前崩溃，修复后正常运行）

### 4.10 后续优化方向
1. **按需分配优化**：目前仍在 `do_exec` 时预分配所有代码段，可改为真正的按需分配
2. **内存回收**：实现 `freePage` 和 `free_pgtable_pages`，支持进程退出时回收内存
3. **页面换出**：实现 Task 3 的 Swap 机制，支持物理内存不足时的页面置换
4. **写时复制**：优化 `fork` 系统调用，实现 Copy-on-Write

## 5. P3 测试程序与 P4 虚拟内存的兼容性问题

### 5.1 问题发现

在使用 P3 的 `condition` 测试程序验证 P4 的虚拟内存实现时，发现 **consumer 进程永远阻塞**，无法消费 producer 生产的产品。

### 5.2 问题分析

**P3 测试程序的设计假设**：所有进程共享同一个物理地址空间。

`condition.c` 测试程序使用固定地址 `0x56000000` 作为共享变量：
```c
#define RESOURCE_ADDR 0x56000000
*(int *)RESOURCE_ADDR = 0;  // 主进程初始化
```

Producer 和 Consumer 子进程通过这个地址通信：
- Producer: `(*num_staff) += production;`
- Consumer: `while (*(num_staff) == 0) { sys_condition_wait(...); }`

**P4 引入虚拟内存后的实际情况**：

每个进程拥有独立的地址空间，相同的虚拟地址映射到**不同的物理页**：
- `condition` 进程：`VA 0x56000000` → `PA A`
- `producer` 进程：`VA 0x56000000` → `PA B`（缺页时新分配）
- `consumer` 进程：`VA 0x56000000` → `PA C`（缺页时新分配）

**结果**：Producer 在物理页 B 上写入数据，Consumer 在物理页 C 上读取数据。由于两者操作的是不同的物理内存，Consumer 永远看不到 Producer 的写入，导致永久阻塞。

### 5.3 为什么 Barrier 测试程序没有问题？

`barrier.c` 测试程序只传递整数句柄给子进程，**不依赖用户态共享内存**。所有同步状态保存在内核数据结构 `barrs[]` 中，通过系统调用访问，不受地址空间隔离影响。

### 5.4 结论

这不是内核实现的 Bug，而是 **P3 测试程序的设计与 P4 虚拟内存机制不兼容**。

正确的进程间通信应该使用：
1. **共享内存系统调用**（Task 4 的 `shm_page_get`）
2. **管道或消息队列**
3. **内核维护的同步原语**（如 barrier、semaphore）

### 5.5 临时解决方案（已撤销）

曾尝试在缺页处理中对 `0x50000000 - 0x60000000` 地址范围使用恒等映射，使所有进程共享同一物理页。但这只是权宜之计，不符合虚拟内存的设计原则，故已撤销。

---


## 6. Task 2 & 3 & 4 实验总结

### 6.1 实验完成情况

本阶段完成了 **A-core / C-core** 的核心要求：

1. **任务二：按需调页（On-demand Paging）**
   - 实现了缺页异常处理程序 [`handle_page_fault()`](kernel/irq/irq.c:33)
   - 支持动态页表分配，只在实际访问时才分配物理页框
   - 通过 `fly` 和 `rw` 测试程序验证功能正确性

2. **任务三：换页机制（Page Swap）**
   - 实现了基于 Clock 算法的页面替换机制
   - 在 SD 卡上建立了 swap 空间管理
   - 实现了页面换出 [`swap_out_page()`](kernel/mm/mm.c:70) 和换入 [`swap_in_page()`](kernel/mm/mm.c:133) 功能
   - 支持物理内存不足时自动换页

3. **任务四：系统可用内存查看**
   - 实现了 [`sys_get_free_memory()`](kernel/mm/mm.c:219) 系统调用
   - 在 Shell 中添加了 `free` 命令，支持 `-h` 选项显示人类可读格式
   - 可实时查看系统剩余物理内存

### 6.2 关键实现细节

#### 6.2.1 缺页处理机制 (`irq.c`)

在 [`handle_page_fault()`](kernel/irq/irq.c:33) 中实现了两种缺页情况的处理：

- **换页缺页**：检查 PTE 的 `_PAGE_SOFT` 位，如果页面已被换出到 SD 卡，则调用 [`swap_in_page()`](kernel/mm/mm.c:133) 将其换入
- **首次访问缺页**：调用 [`alloc_page_helper()`](kernel/mm/mm.c:285) 为虚拟地址分配新的物理页框

```c
void handle_page_fault(regs_context_t *regs, uint64_t stval, uint64_t scause){
    uintptr_t fault_va = stval & ~(PAGE_SIZE - 1);
    
    // Check if page is swapped out
    uintptr_t pte_ptr = get_pteptr_of(fault_va, current_running->pgdir);
    if (pte_ptr) {
        PTE *pte = (PTE *)pte_ptr;
        if ((*pte & _PAGE_SOFT) && !(*pte & _PAGE_PRESENT)) {
            int swap_idx = (*pte >> _PAGE_PFN_SHIFT) & 0x3FF;
            swap_in_page(fault_va, current_running->pgdir, swap_idx);
            local_flush_tlb_page(fault_va);
            return;
        }
    }
    
    alloc_page_helper(fault_va, current_running->pgdir);
    local_flush_tlb_page(fault_va);
}
```

#### 6.2.2 换页机制 (`mm.c`)

**数据结构设计**：
- [`page_frame_t`](kernel/mm/mm.c:14)：页框管理结构，记录虚拟地址、页目录、swap 索引、引用位等信息
- [`page_frames[]`](kernel/mm/mm.c:23)：全局页框数组，管理所有物理页框
- [`clock_hand`](kernel/mm/mm.c:24)：Clock 算法的指针
- [`swap_bitmap[]`](kernel/mm/mm.c:25)：SD 卡 swap 空间的位图管理

**Clock 页面替换算法**：
```c
int swap_out_page() {
    while (attempts < TOTAL_PHYSICAL_PAGES * 2) {
        if (clock_hand->in_use && clock_hand->pgdir != 0) {
            if (clock_hand->va < 0x8000000000000000UL) {  // 用户页
                if (clock_hand->reference_bit == 0) {
                    // 找到牺牲页，换出到 SD 卡
                    // ...
                } else {
                    clock_hand->reference_bit = 0;  // 给第二次机会
                }
            }
        }
        clock_hand = clock_hand->next;
        attempts++;
    }
}
```

**SD 卡 Swap 空间管理**：
- 起始扇区：`SWAP_START_SECTOR = 0x200000`
- 最大页数：`MAX_SWAP_PAGES = 1024`
- 每页占用 8 个扇区（4KB = 8 × 512B）

#### 6.2.3 内存统计功能

[`get_free_memory()`](kernel/mm/mm.c:219) 实现：
- 遍历空闲链表统计空闲页数
- 计算未分配的内核内存
- 返回总的可用字节数

Shell 中的 `free` 命令支持：
```bash
> free          # 显示字节数
> free -h       # 人类可读格式（MB/KB/Bytes）
```

### 6.3 遇到的主要问题与解决方案

| 问题现象 | 原因分析 | 解决方案 |
| :--- | :--- | :--- |
| **换页后程序崩溃** | PTE 中 swap 索引的存储位置不正确，导致换入时读取错误数据 | 将 swap 索引存储在 PTE 的 PFN 字段（10-53位），并设置 `_PAGE_SOFT` 标志位 |
| **Clock 算法死循环** | 所有页面的 reference_bit 都为 1，无法找到牺牲页 | 增加最大尝试次数限制，并在第二轮扫描时强制选择页面 |
| **SD 卡读写失败** | 一次性读写扇区数过多导致 BIOS 函数失败 | 限制每次最多读写 8 个扇区（一个页面） |
| **内存泄漏** | 换出页面后未正确释放物理页框 | 在 [`swap_out_page()`](kernel/mm/mm.c:70) 中调用 [`freePage()`](kernel/mm/mm.c:208) 释放页框 |
| **TLB 一致性问题** | 换页后未刷新 TLB，导致访问旧的映射 | 在换入/换出后都调用 [`local_flush_tlb_page()`](arch/riscv/include/pgtable.h:26) |

### 6.4 性能优化

1. **延迟换页触发**：只有当物理内存使用率超过阈值（`TOTAL_PHYSICAL_PAGES - 100`）时才触发换页
2. **批量 TLB 刷新**：使用 `local_flush_tlb_page()` 只刷新单个页面，而非全局刷新
3. **空闲链表管理**：使用链表管理释放的页框，提高分配效率

### 6.5 测试验证

1. **按需调页测试**：
   - 运行 `fly` 程序，验证动态页面分配
   - 运行 `rw` 程序，测试读写不同地址的页面

2. **换页机制测试**：
   - 分配大量内存触发换页
   - 验证换出和换入的正确性
   - 检查 swap 空间的分配和释放

3. **内存统计测试**：
   - 使用 `free` 命令查看初始内存
   - 运行程序后再次查看，验证内存变化
   - 使用 `free -h` 验证人类可读格式

### 6.6 后续改进方向

1. **更高级的替换算法**：可以实现 LRU 或 LFU 算法，提高换页效率
2. **页面预取**：预测即将访问的页面，提前换入
3. **压缩换页**：在换出时压缩页面内容，节省 SD 卡空间
4. **多级 swap**：支持多个 swap 分区，提高并发性能
5. **内存碎片整理**：定期整理物理内存，减少碎片

### 6.7 关键代码文件清单

- [`kernel/mm/mm.c`](kernel/mm/mm.c)：内存管理核心实现
  - [`init_swap()`](kernel/mm/mm.c:29)：初始化换页机制
  - [`swap_out_page()`](kernel/mm/mm.c:70)：页面换出
  - [`swap_in_page()`](kernel/mm/mm.c:133)：页面换入
  - [`get_free_memory()`](kernel/mm/mm.c:219)：获取可用内存
  
- [`kernel/irq/irq.c`](kernel/irq/irq.c)：异常处理
  - [`handle_page_fault()`](kernel/irq/irq.c:33)：缺页异常处理
  
- [`test/shell.c`](test/shell.c)：Shell 命令
  - `free` 命令实现（第 185-199 行）
  
- [`include/os/mm.h`](include/os/mm.h)：内存管理接口定义
  - Swap 相关宏定义和函数声明

---

## 7. Task 5: 零拷贝IPC - 内存页管道

### 7.1 功能概述

实现了基于页面重映射的零拷贝进程间通信（IPC）机制。与传统IPC（通过内核缓冲区复制数据）不同，内存页管道直接将物理页面的所有权从一个进程转移到另一个进程，避免了数据复制，大大提高了大数据量传输的效率。

### 7.2 核心设计与实现

#### 7.2.1 系统调用接口

新增了三个系统调用：
```c
int sys_pipe_open(const char *name);           // 打开/创建命名管道
long sys_pipe_give_pages(int pipe, void *addr, size_t len);  // 给出页面
long sys_pipe_take_pages(int pipe, void *addr, size_t len);  // 获取页面
```

#### 7.2.2 管道数据结构

[`pipe_t`](include/os/mm.h:84) 结构定义：
```c
typedef struct pipe {
    char name[32];               // 管道名称（用于命名管道）
    int ref_count;               // 引用计数（多进程共享）
    pipe_page_t *head;           // 页面链表头（FIFO）
    pipe_page_t *tail;           // 页面链表尾
    int total_pages;             // 当前管道中的页面数
    int is_open;                 // 管道状态
} pipe_t;
```

[`pipe_page_t`](include/os/mm.h:77) 页面描述符：
```c
typedef struct pipe_page {
    void *kva;                   // 内核虚拟地址
    uintptr_t pa;                // 物理地址
    int in_use;                  // 使用标志
    int size;                    // 数据大小（可能小于PAGE_SIZE）
    struct pipe_page *next;      // 链表指针
} pipe_page_t;
```

#### 7.2.3 零拷贝传输机制

**发送方（give_pages）**：
1. 遍历要传输的页面，获取每个页面的物理地址
2. 创建 `pipe_page_t` 结构描述物理页面
3. 将页面描述符添加到管道的FIFO队列
4. **关键**：物理页面保持在原进程的页表中，但标记为待传输

**接收方（take_pages）**：
1. 从管道头部取出页面描述符
2. 修改接收进程页表，将虚拟地址映射到该物理页面
3. 刷新TLB，确保新映射生效
4. **关键**：直接重用物理页面，无需数据复制

#### 7.2.4 进程管道跟踪

为了防止资源泄漏，每个进程维护一个打开的管道列表：
- [`pcb_t.open_pipes[MAX_PROCESS_PIPES]`](include/os/sched.h:103)：存储打开的管道ID
- [`pcb_t.num_open_pipes`](include/os/sched.h:104)：打开的管道数量
- 进程退出时自动调用 [`do_pipe_close()`](kernel/mm/mm.c:381) 清理资源

### 7.3 遇到的主要问题与解决方案

| 问题现象 | 原因分析 | 解决方案 |
|---------|---------|---------|
| **多次运行后系统崩溃** | 进程退出时未关闭管道，导致管道资源泄漏 | 实现进程管道跟踪，在 `do_exit()` 中自动清理 |
| **get_pteptr_of地址错误** | 错误的类型转换导致地址计算错误 | 修复类型转换：`(uintptr_t *)` → `PTE *` |
| **管道程序死锁** | 接收方在发送方give之前take导致无限等待 | 修改管道返回0当无数据，而非阻塞等待 |
| **内存显示223MB** | `get_free_memory()` 使用硬编码地址 | 改用 `TOTAL_PHYSICAL_PAGES` 计算 |
| **编译错误** | `MAX_PROCESS_PIPES` 未定义 | 移到 `sched.h` 中定义 |

### 7.4 性能分析

#### 7.4.1 零拷贝优势
1. **内存效率**：
   - 传统IPC：数据复制两次（发送方→内核→接收方）
   - 页面管道：仅修改页表映射，零数据拷贝
   - 对于传输1页数据，节省8KB内存带宽

2. **CPU节省**：
   - 避免了 `memcpy` 操作
   - 减少缓存污染
   - CPU周期仅用于页表更新

#### 7.4.2 适用场景
- 大数据量传输（如视频流）
- 高频IPC场景
- 内存敏感型应用
- 多媒体数据处理

### 7.5 测试验证

#### 7.5.1 基础功能测试
```bash
> exec pipe_self            # 同进程内测试
# 输出：pipe self: success, msg="hello-from-pipe-self"
```

#### 7.5.2 跨进程通信测试
```bash
> exec pipe send &           # 后台启动发送进程
> exec pipe recv             # 启动接收进程
# 输出：成功传输页面数据
```

#### 7.5.3 资源管理测试
```bash
> exec pipe                  # 第一次运行
> exec pipe                  # 第二次运行
> exec pipe                  # 第三次运行
# 所有运行都应该成功，无资源泄漏
```

#### 7.5.4 内存限制下的测试
```bash
> free -h                    # 查看内存（应显示2MB）
> exec pipe                  # 在受限内存下测试
> exec mem_stress2           # 同时触发swap
# 验证管道与swap机制协同工作
```

### 7.6 关键代码位置

- [`do_pipe_open()`](kernel/mm/mm.c:412)：管道打开/创建
- [`do_pipe_give_pages()`](kernel/mm/mm.c:470)：页面给出实现
- [`do_pipe_take_pages()`](kernel/mm/mm.c:535)：页面获取实现
- [`do_pipe_close()`](kernel/mm/mm.c:381)：管道关闭和资源清理
- [`get_pteptr_of()`](arch/riscv/include/pgtable.h:109)：获取页表项指针（已修复）

### 7.7 系统配置

当前系统配置：
- **物理内存**：512页 = 2MB（便于测试swap）
- **最大管道数**：32个
- **每进程最大管道数**：8个
- **Swap空间**：1024页（4MB）

调整内存大小：
```c
// include/os/mm.h
#define TOTAL_PHYSICAL_PAGES 512     // 2MB
// 或：
#define TOTAL_PHYSICAL_PAGES 2048    // 8MB
// 或：
#define TOTAL_PHYSICAL_PAGES 57344   // 224MB（原始大小）
```

---
