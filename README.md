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

## 三、任务一：发送（Task 1）

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

### 3. 测试与验证

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

### 4. 避坑指南总结

- 物理地址是红线：所有给网卡看的（基址、描述符内的地址）必须是物理地址
- Cache/一致性是死穴：写描述符后不做同步，网卡可能读到全 0 或旧数据
- `TDT` 的含义：`TDT` 写入的是索引值（比如 `1, 2, 3`），不是字节偏移
- 防御性编程：操作数值字段（`CT`/`COLD`）时始终配合掩码，防止左移超出预定区间污染其他控制位
- 关于轮询：任务一允许自旋，但会导致 CPU 占用；进阶方案是调用 `do_scheduler()`，但这在 Task 3 中会通过中断彻底解决

## 四、任务二：接收（Task 2）

本节整理 Task 2 的接收端实现要点：`e1000_configure_rx`、`e1000_poll`、`do_net_recv` 以及对应测试方法。

### 1. 核心概念：RX 描述符环

- RX 描述符（`struct e1000_rx_desc`）由软件提前填好 `addr`（接收缓冲区物理地址），硬件收到包后会 DMA 写入缓冲区，并回填 `length/status`。
- `RDH`（硬件 Head）与 `RDT`（软件 Tail）共同限定硬件可用的描述符范围；软件处理完一个槽位后必须推进 `RDT` 把槽位归还给硬件。

### 2. 初始化：`e1000_configure_rx`

- MAC 地址：写 `RAR[0]`（`RAL0/RAH0`），并置位 `RAH0` 的 Address Valid（第 31 位），否则非广播包会被丢弃。
- 描述符环：写 `RDBAL/RDBAH` 为描述符数组物理地址，写 `RDLEN` 为总字节数（`RXDESCS * sizeof(struct e1000_rx_desc)`）。
- 指针：通常设置 `RDH = 0`，`RDT = RXDESCS - 1`，使硬件初始即可使用整圈描述符。
- `RCTL`：至少开启 `EN`（接收使能），并按需要开启 `BAM`（接收广播）；缓冲区大小配置应与 `RX_PKT_SIZE` 匹配（如 `2048`）。

### 3. 轮询收包：`e1000_poll`

建议语义：

- 无包可取：返回 `0`
- 收到 1 个包：拷贝到 `rxbuffer` 并返回 `> 0` 的包长

关键动作：

- 检查 `DD`：未置位表示硬件尚未写完该槽位。
- 处理完成后清 `status`（把描述符“还给硬件”），并写 `E1000_RDT` 推进 Tail。

### 4. 系统调用层：`do_net_recv`

常见实现是循环调用 `e1000_poll` 收 `pkt_num` 个包，将每个包的长度写入 `pkt_lens[i]`，并把包数据按顺序连续写入 `rxbuffer`，返回总字节数。

### 5. 测试：`recv` / `recv2`

- QEMU 侧运行接收程序（注意 `recv2` 的参数解析不支持空格）：

```text
exec recv &
exec recv2 -n72 -l80 -c &
```

- Host 侧使用 `tools/pkt-rx-tx-master` 对 `tap0` 发包，然后观察 QEMU 输出。

### 6. 常见坑

- `RAH0` 的 Address Valid 位没置：只会看到广播，单播包收不到。
- `RDT` 初始值错误：设置为 `RXDESCS - 1` 才能把整圈描述符交给硬件。
- 描述符/缓冲区地址：给硬件的一律是物理地址（需要 `kva2pa/va2pa`）。

## 五、任务三：中断（Task 3）

Task 3 的目标是把忙轮询改为“中断触发 + 进程阻塞”，让 CPU 在设备未就绪时调度其他任务。

### 1. 外部中断的四级路径

1) 硬件与 stvec
- 网卡向 PLIC 发中断，PLIC 仲裁后通知 CPU
- CPU 跳转到 stvec（exception_handler）

2) scause 判定
- scause == IRQ_S_EXT 进入外部中断处理

3) PLIC claim
- 读取 claim 得到中断源 ID（QEMU 通常为 33）

4) E1000 ICR 原因位
- TXQE：发送队列空（TDH == TDT），表示可继续发
- RXDMT0：可用接收描述符低于阈值（RCTL.RXDMT=0 表示 1/2）

### 2. 中断寄存器组

- ICR：读清零，处理中断时必须先读
- IMS：写 1 使能
- IMC：写 1 禁用
- ICS：软件触发（调试用）

### 3. 从轮询到阻塞的代码改造

发送：队列满时阻塞，等待 TXQE 唤醒
```c
while (!(tx_desc[tail].status & DD)) {
    do_block(&send_wait_queue);
}
```

接收：无包时阻塞，等待 RXDMT0 唤醒
```c
while (!(rx_desc[head].status & DD)) {
    do_block(&recv_wait_queue);
}
```

### 4. 实现步骤清单

1) `e1000_init`：开启 IMS 中的 TXQE 和 RXDMT0；设置 `RCTL.RXDMT = 0`
2) `kernel/irq/irq.c`：外部中断里 `plic_claim`，识别网卡 ID 后调用 `e1000_handle_irq`
3) `drivers/e1000.c`：读取 ICR，按位分别唤醒 send/recv 队列
4) `plic_complete(irq_id)`：必须调用，避免只触发一次

### 5. 避坑清单

- 操作等待队列前注意原子性，否则会丢唤醒
- ICR 读清零，非中断处理处不要随意读
- 忘记 `plic_complete` 会导致只收到一次外部中断

### 6. 终端卡死 Bug 的调试过程与修复

现象：
- `exec recv &` 后发送包，终端无输出且无法输入
- e1000 收到包但系统“像死机”

关键定位过程：
1) 在 `drivers/e1000.c` 的接收拷贝处打点，出现
   - `copy enter` 但没有任何后续字节拷贝日志
2) 查看 `rxbuffer` 页表项，P/W/U 都为 1，说明映射存在且可写
3) 结论：不是页表映射问题，而是内核态访问用户页被禁止

根因：
- 内核态写用户缓冲触发异常（SUM=0），进入异常后再次尝试获取内核大锁导致死锁

修复：
- 在 `arch/riscv/kernel/head.S` 中设置 `SR_SUM`，允许 S 态访问用户页
  ```asm
  li t0, SR_SUM
  csrs CSR_SSTATUS, t0
  ```
- 修复后 `e1000_poll` 的用户缓冲拷贝不再触发异常，终端恢复正常

## 六、任务四：可靠传输（Task 4）

任务四（C-core 任务）要求在不可靠的数据链路层之上，实现一个简化的传输层协议，以支持可靠的、顺序的文件或流数据传输。

### 1. 协议概览

本项目使用简化传输层协议，核心特点：
- 无连接：不区分连接，所有包统一处理
- 单向传输：发送方主动发起，接收方监听
- 接收方驱动重传：由接收方发 RSD 触发重传
- 无流量/拥塞控制：窗口大小为常数，不做动态调整

### 2. 报文结构 (Packet Structure)

协议起始位置位于数据帧第 55 个字节（前 54 字节为 TCP/IP 报头），报头 8 字节：
- `magic`(1B)：固定 `0x45`
- `flags`(1B)：DAT/ACK/RSD
- `len`(2B)：数据部分长度（不含报头）
- `seq`(4B)：数据包序号（字节偏移）

注：文件传输时 `seq=0` 的 Data 前 4 字节存放文件大小（应用层逻辑，内核不使用）。

### 3. 三种核心操作

- DAT (Data)：发送方发送数据包，`seq` 为数据在流中的起始偏移
- ACK (Acknowledgement)：接收方确认，`seq` 表示小于该值的数据已顺序到齐
- RSD (Retransmission Request)：接收方请求重传，`seq` 为缺失包起始偏移

### 4. 系统调用实现

新增系统调用：
`sys_net_recv_stream(void *buffer, int *nbytes)`

- 接收最多 `*nbytes` 字节写入 `buffer`
- 实际接收字节数写回 `*nbytes`
- 需处理乱序与丢包，并基于定时机制触发 RSD

### 5. 用户程序与校验

用户程序接收大文件，计算 Fletcher 校验和并打印：
```c
uint16_t fletcher16(uint8_t *data, int n) {
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    int i;
    for (i = 0; i < n; ++i) {
        sum1 = (sum1 + data[i]) % 0xff;
        sum2 = (sum2 + sum1) % 0xff;
    }
    return (sum2 << 8) | sum1;
}
```

### 6. 注意事项与实现细节

- 字节序：报头 `len/seq` 为网络字节序（大端）
- 协议位置：固定从数据帧第 55 字节开始
- 丢包模拟：pktRxTx 支持 SEND_RELIABLE + 丢包/乱序比例
- 重传触发：乱序包缓存 + 定时 RSD
- 发送方限制：ACK 不及时会导致窗口被占满
- RSD 校验：pktRxTx 若打印 RSD with error，说明请求了无效或已 ACK 的 `seq`

### 7. 调试记录与修复

可靠接收无响应：
- 现象：发送端超时，收不到 ACK/RSD
- 根因：仅开启 RXDMT0，小流量不足以触发中断，接收线程阻塞
- 修复：不依赖 `RXT0`，改为在时钟中断里定时唤醒流式接收线程，并将流式接收与 Task3 的阻塞队列隔离

### 8. 测试方法（完整命令）

OS 侧：
```text
exec recv_stream -f
```

Host 侧（发送大文件，支持丢包/乱序参数）：
```bash
cd /home/stu/ucas-os-kernel/tools/pkt-rx-tx-master/pktRxTx-Linux
sudo ./pktRxTx -m 5 -f /home/stu/ucas-os-kernel/build/main -w 10000 -l 0 -s 0 -t 50
```

Host 侧校验：
```bash
python3 - <<'PY'
data = open('/home/stu/ucas-os-kernel/build/main','rb').read()
sum1 = 0; sum2 = 0
for b in data:
    sum1 = (sum1 + b) % 0xff
    sum2 = (sum2 + sum1) % 0xff
print(f"host fletcher16=0x{((sum2<<8)|sum1):04x}")
PY
```

### 9. 本次修改补充

- 兼容板卡中断类型不足的场景：不再依赖 `E1000_ICR_RXT0`
- Task4 流式接收使用独立阻塞队列，避免影响 Task3 的 RXDMT0 路径
- 使用时钟中断定时唤醒流式接收线程，低流量也能推进 ACK/RSD
