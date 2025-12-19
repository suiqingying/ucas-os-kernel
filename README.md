<div align="center">
  <h1>UCAS 操作系统研讨课 (OS Kernel)</h1>
  <h3>个人实现与学习笔记</h3>
  
  <p>
    <img src="https://img.shields.io/badge/Architecture-RISC--V-black?style=for-the-badge&logo=riscv" alt="RISC-V" />
    <img src="https://img.shields.io/badge/Language-C%20%2F%20Assembly-blue?style=for-the-badge&logo=c" alt="Language" />
    <img src="https://img.shields.io/badge/Status-Course%20Completed-success?style=for-the-badge" alt="Status" />
  </p>

  <img src="./docs/assets/architecture_overview.png" width="80%" alt="UCAS-OS 操作系统架构概览" />

  <p>
    <a href="../../tree/Prj1"><img alt="Start Prj1" src="https://img.shields.io/badge/Start-Prj1-7C3AED?style=for-the-badge" /></a>
    <a href="../../tree/Prj4"><img alt="VM Prj4" src="https://img.shields.io/badge/Virtual%20Memory-Prj4-22D3EE?style=for-the-badge" /></a>
    <a href="../../tree/Prj5"><img alt="Net Prj5" src="https://img.shields.io/badge/Driver%20%26%20Network-Prj5-34D399?style=for-the-badge" /></a>
  </p>

  <p>
    <b>🌟 如果这份代码救了你的头发，请反手点个 Star 予以慰问！🌟</b><br/>
    <i>(听说点过 Star 的同学，内核都不报 Page Fault，实验验收一次过 😉)</i>
  </p>
</div>

---

## 👋 写在前面

这里是我的 **UCAS-OS 操作系统** 课程实验代码仓库。

做这门课的时候踩过不少坑，掉过不少头发。现在回头看，其实很多难点只要理清了思路就能迎刃而解。我把项目拆分到了 **`Prj*` 分支** 中，希望这份代码和笔记能给正在熬夜 Debug 的你一点灵感和指引。如果对你有所启发，记得 star 一下。

> **⚠️ Note：** 代码仅供参考，思路比实现更重要。请务必自己动手写，直接 Copy 不仅过不了查重，也会失去这门硬核课程最大的乐趣（和痛苦）。

## 🗺️ 学习路线与进度

我是按照以下路线从零构建这个 RISC-V 内核的：

<div align="center">
  <img src="./docs/assets/learning_roadmap.png" width="92%" alt="RISC-V OS 学习路线" />
  <p><i>我的通关路径：启动与引导 → 内核核心 → 虚拟内存 → 驱动与文件系统</i></p>
</div>

### 🚀 实验阶段存档

| 阶段 | 实验主题 | 踩坑关键词 | 传送门 |
| :--- | :--- | :--- | :---: |
| **Prj0** | **环境准备** | QEMU 配置, GDB 调试 | [查看](../../tree/main/guide) |
| **Prj1** | **引导与加载** | Bootloader 搬运, ELF 解析 | [Go](../../tree/Prj1) |
| **Prj2** | **内核核心** | 上下文切换, 时钟中断, 锁 | [Go](../../tree/Prj2) |
| **Prj3** | **综合实践** | *Prj1 与 Prj2 的大融合* | [Go](../../tree/Prj3) |
| **Prj4** | **虚拟内存** | Sv39 页表, 缺页处理, TLB 刷新 | [Go](../../tree/Prj4) |
| **Prj5** | **驱动与网络** | E1000 网卡, DMA 描述符, 丢包重传 | [Go](../../tree/Prj5) |
| **Prj6** | **文件系统** | 文件读写 (未完待续/选做) | [Go](../../tree/Prj6) |

> **食用指南：** `Prj5` 是我完成度最高的版本，包含了前面所有的功能，适合查看完整的架构。

---

## 📂 全景代码结构 (以 Prj5 为例)

这是整个项目的地图，我列出了所有一级目录。点击箭头可展开查看子模块：

- <details><summary><b><code>init/</code>: 系统启动入口</b></summary>
    <ul>
      <li><code>main.c</code>: <b>绝对核心</b>。内核的 <code>main</code> 函数，负责初始化子系统并启动 Shell。</li>
    </ul>
  </details>

- <details><summary><b><code>arch/</code>: 架构相关 (RISC-V)</b></summary>
    <ul>
      <li><code>riscv/boot/</code>: <b>启动引导</b>。<code>bootblock.S</code> (加载器) 就在这里。</li>
      <li><code>riscv/kernel/</code>: <b>底层汇编</b>。<code>entry.S</code> (异常/中断入口), <code>head.S</code> (内核入口)。</li>
      <li><code>riscv/include/</code>: <b>硬件定义</b>。寄存器 (CSR)、页表 (PTE)、上下文 (Context) 定义。</li>
    </ul>
  </details>

- <details><summary><b><code>kernel/</code>: 内核核心子系统</b></summary>
    <ul>
      <li><code>sched/</code>: <b>调度器</b>。PCB 结构、调度算法、上下文切换逻辑。</li>
      <li><code>syscall/</code>: <b>系统调用</b>。处理用户态发来的 <code>ecall</code> 请求。</li>
      <li><code>mm/</code>: <b>内存管理</b>。物理页分配、虚实地址映射、缺页异常处理。</li>
      <li><code>locking/</code>: <b>同步原语</b>。自旋锁 (Spinlock)、互斥锁 (Mutex)、栅栏。</li>
      <li><code>irq/</code>: <b>中断管理</b>。中断分发与路由。</li>
      <li><code>net/</code>: <b>网络协议栈</b>。简易的 TCP/IP 栈实现 (Prj5)。</li>
      <li><code>loader/</code>: <b>加载器</b>。解析 ELF 文件，把用户程序搬进内存。</li>
      <li><code>smp/</code>: <b>多核支持</b>。多核启动与核间同步。</li>
    </ul>
  </details>

- <details><summary><b><code>drivers/</code>: 设备驱动</b></summary>
    <ul>
      <li><code>e1000.c</code>: <b>网卡驱动</b>。最硬核的驱动，涉及 DMA 描述符环和寄存器配置。</li>
      <li><code>plic.c</code>: <b>中断控制器</b>。管理外部中断。</li>
      <li><code>screen.c</code>: <b>屏幕驱动</b>。基础的字符输出。</li>
    </ul>
  </details>

- <details><summary><b><code>include/</code>: 头文件接口</b></summary>
    <ul>
      <li><code>os/</code>: <b>内核头文件</b>。声明了各子系统的结构体和函数。</li>
      <li><code>sys/</code>: <b>系统标准</b>。定义了系统调用号 (syscall numbers)。</li>
    </ul>
  </details>

- <details><summary><b><code>libs/</code>: 内核基础库</b></summary>
    <ul>
      <li><code>printk.c</code>: <b>调试神器</b>。内核版的 printf 实现。</li>
      <li><code>string.c</code>: <code>memcpy</code>, <code>memset</code>, <code>strcpy</code> 等常用工具。</li>
    </ul>
  </details>

- <details><summary><b><code>test/</code>: 测试用例</b></summary>
    <ul>
      <li><code>test_project*/</code>: 对应各个 Project 的验收程序 (如 <code>test_project5</code>)。</li>
    </ul>
  </details>

- <details><summary><b><code>tiny_libc/</code>: 用户态 C 库</b></summary>
    <ul>
      <li>给用户程序用的迷你标准库 (<code>printf</code>, <code>malloc</code>, <code>syscall</code> 封装)。</li>
    </ul>
  </details>

- <details><summary><b><code>tools/</code>: 构建工具</b></summary>
    <ul>
      <li><code>createimage.c</code>: <b>镜像打包器</b>。读取编译好的内核和程序，制作成 QEMU 可启动的镜像文件。</li>
    </ul>
  </details>

---

## 💡 给学弟学妹的“防脱发”建议

1.  **printk 是真正的调试神器：**
    *   别迷信复杂的工具，`printk` 往往是最直观、最高效的选择。
    *   通过在关键位置打 log，你可以清晰地看到内核运行的“时序”和“轨迹”。
    *   **Tip：** 多打一些十六进制的地址和寄存器值，很多时候 Page Fault 就是因为某个指针算错了。
2.  **GDB 作为辅助：**
    *   当 `printk` 解决不了（比如系统启动前就挂了，或者需要肉眼看汇编单步执行）时，再祭出 GDB。
    *   学会看内核栈和寄存器状态，它是你最后的救命稻草。
3.  **理解比代码重要：**
    *   抄代码没意义，关键是理解 *为什么* 要在这个地方关中断，*为什么* 上下文切换要保存这几个寄存器。
    *   我也在代码里留了一些注释（虽然不多），希望能帮到你。
4.  **心态要稳：**
    *   环境配半天、Bootblock 跑不起来、Page Fault 连环爆……这些都是日常。
    *   解决掉 Bug 的那一刻，你会觉得操作系统真有意思。

## ⚡ 快速查看

如果你想看具体的实现，可以切过去：

```bash
# 看看我是怎么写引导程序的
git checkout Prj1

# 看看最复杂的网络驱动部分
git checkout Prj5
```

Good Luck! 🤞