# Project 5：E1000 网卡驱动开发全攻略

本指南旨在为国科大操作系统研讨课 Project 5 提供最详尽的理论与实践支持。内容涵盖了从 DMA 原理到发送逻辑实现的每一个技术细节。

## 一、核心理论：硬件与软件的“契约”

在编写代码前，必须理解 Intel E1000 网卡如何通过 DMA (Direct Memory Access) 与内核交互。

### 1. 描述符 (Descriptor)：沟通的语言

网卡硬件无法理解 C 语言逻辑，它通过一种 16 字节的固定数据结构——描述符来获取任务。

发送描述符 (TX Descriptor) 结构 (Legacy 模式)：

- `Buffer Address [63:0]`：数据包在内存中的物理地址
- `Length [15:0]`：数据包的字节长度
- `CMD [7:0]` (Command)：控制位
  - `EOP` (End of Packet)：标识这是数据包的最后一个描述符
  - `RS` (Report Status)：要求硬件在处理完后写回状态（即设置 `DD` 位）
- `STA [3:0]` (Status)：硬件反馈位
  - `DD` (Descriptor Done)：硬件已完成发送，软件可以回收此描述符

### 2. 描述符环形队列 (The Ring Buffer)

描述符在内存中以数组形式组织，逻辑上首尾相连形成一个环。

- `TDH` (Transmit Descriptor Head)：硬件控制。指向网卡当前正在处理或即将处理的描述符
- `TDT` (Transmit Descriptor Tail)：软件控制。指向软件下一个可以填入数据的位置

工作流程：

1. 软件将包信息填入 `TDT` 指向的坑位，然后更新 `TDT = (TDT + 1) % DDLEN`
2. 硬件发现 `TDH != TDT`，开始从 `TDH` 搬运数据并发送，完成后挪动 `TDH`

判满条件：若 `(TDT + 1) % DDLEN == TDH`，说明环已满，软件必须等待。

## 二、基础设施：地址映射 (ioremap)

### 1. 为什么需要 ioremap？

网卡寄存器位于物理内存地址空间。在开启虚存的内核中，CPU 无法直接通过物理地址访问它们。你必须在页表中建立映射。

### 2. 实现要点

- 映射范围：将网卡的物理基址（通过 `bios_read_fdt` 获取）映射到内核的一段虚地址
- 页表项 (PTE) 标志位：映射时，必须确保页表项的 `A` (Accessed) 和 `D` (Dirty) 位被置为 `1`；否则内核访问这些 MMIO 地址时可能触发异常，或在写入时导致死锁

## 三、任务一：轮询发送逻辑拆解 (Task 1)

### 1. 发送配置初始化 (e1000_configure_tx)

你需要按照硬件手册（14.5 节）的顺序设置寄存器：

1. 基址寄存器：将描述符环数组的物理地址写入 `TDBAL`（低 32 位）和 `TDBAH`（高 32 位）
2. 长度寄存器：将数组总字节数（数量 * 16）写入 `TDLEN`（必须 128 字节对齐）
3. 指针清零：初始化 `TDH = 0` 和 `TDT = 0`
4. 配置 `TCTL` (Transmit Control Register)

`TCTL` 位字段与掩码说明：

- `EN` (Bit 1)：总开关
- `PSP` (Bit 3)：短包填充
- `CT` (Bits 4:11)：冲突阈值（掩码 `0x00000ff0`；这是数值字段，不是 One-hot）
- `COLD` (Bits 12:21)：冲突距离（掩码 `0x003ff000`）

推荐设置值：`EN=1, PSP=1, CT=0x10, COLD=0x40`。

操作模式（读-改-写）：

```c
uint32_t tctl = e1000_read_reg(E1000_TCTL);
tctl &= ~(E1000_TCTL_CT | E1000_TCTL_COLD); // 清空旧字段
tctl |= (0x10 << 4) & E1000_TCTL_CT;        // 防御性编程：移位后按位与掩码
tctl |= (0x40 << 12) & E1000_TCTL_COLD;
tctl |= E1000_TCTL_EN | E1000_TCTL_PSP;
e1000_write_reg(E1000_TCTL, tctl);
```

### 2. 发送函数实现 (e1000_transmit) —— 软件维护指针优化

优化逻辑：与其每次都 MMIO 读取 `TDT`（非常慢），不如在软件中维护一个静态变量 `static uint32_t tx_tail = 0;`。

完整步骤：

1. 获取当前位置：直接使用内存中的 `tx_tail`
2. 检查可用性（`DD` 位）
   - 检查 `tx_desc[tx_tail].status` 的 `DD` 位
   - 轮询 (Polling/Spinning)：若 `DD == 0`，则 `while` 循环等待（Task 1 允许）
   - 注意：初次发包时描述符状态可能全为 `0`，建议初始化时将所有描述符的 `status` 预设为 `E1000_TXD_STAT_DD`
3. 填充描述符
   - 地址转换：`tx_desc[tx_tail].buffer_addr = va2pa(data);`
   - 设置长度：`tx_desc[tx_tail].lower.data = len;`
   - 设置命令：必须包含 `E1000_TXD_CMD_EOP` 和 `E1000_TXD_CMD_RS`
   - 清空状态：`tx_desc[tx_tail].upper.data = 0;`（清除旧的 `DD` 位）
4. 内存同步 (Fence)
   - 在写寄存器前调用 `flush_dcache()` 或 `mb()`，保证网卡 DMA 读到的是新数据
5. 通知硬件
   - 更新软件指针：`tx_tail = (tx_tail + 1) % TX_DESC_COUNT;`
   - 写寄存器：`e1000_write_reg(E1000_TDT, tx_tail);`（写入后网卡立即开工）

### 3. 调试与测试验证

#### 1. 抓包验证

- 工具：`tcpdump`
- 指令：

```bash
sudo tcpdump -i tap0 -XX -vvv -nn
```

- 现象：运行 `exec send` 后，该窗口应跳出以太网帧

#### 2. 边界条件：支持发包数 > 描述符数

- 测试方法：如果环大小为 `16`，尝试发 `64` 个包
- 预期逻辑：由于有 `while` 轮询 `DD` 位，发满 `16` 个后，`transmit` 函数会卡住（自旋）等待网卡发完腾出空位；直到所有包发送完毕，系统不应崩溃

### 4、避坑指南总结

- 物理地址是红线：所有给网卡看的（基址、描述符内的地址）必须是物理地址
- Cache/一致性是死穴：写描述符后不做同步，网卡可能读到全 0 或旧数据
- `TDT` 的含义：`TDT` 写入的是索引值（比如 `1, 2, 3`），不是字节偏移
- 防御性编程：操作数值字段（`CT`/`COLD`）时始终配合掩码，防止左移超出预定区间污染其他控制位
- 关于轮询：任务一允许自旋，但会导致 CPU 占用；进阶方案是调用 `do_scheduler()`，但这在 Task 3 中会通过中断彻底解决

