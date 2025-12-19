<div align="center">
  <img src="./assets/architecture_overview.png" width="100%" alt="UCAS-OS 操作系统架构概览" />
</div>

<div align="center">
  <img src="https://img.shields.io/badge/Architecture-RISC--V-black?style=for-the-badge&logo=riscv" alt="RISC-V" />
  <img src="https://img.shields.io/badge/Language-C%20%2F%20Assembly-blue?style=for-the-badge&logo=c" alt="Language" />
  <img src="https://img.shields.io/badge/Status-Course%20Completed-success?style=for-the-badge" alt="Status" />
</div>

<div align="center">
  <h3>UCAS 操作系统研讨课 (OS Kernel) 个人实现与学习笔记</h3>
  <a href="../../tree/Prj1"><img alt="Start Prj1" src="https://img.shields.io/badge/Start-Prj1-7C3AED?style=for-the-badge" /></a>
  <a href="../../tree/Prj4"><img alt="VM Prj4" src="https://img.shields.io/badge/Virtual%20Memory-Prj4-22D3EE?style=for-the-badge" /></a>
  <a href="../../tree/Prj5"><img alt="Net Prj5" src="https://img.shields.io/badge/Driver%20%26%20Network-Prj5-34D399?style=for-the-badge" /></a>
</div>

---

## 👋 写在前面

这里是我的 **UCAS-OS 操作系统** 课程实验代码仓库。

做这门课的时候踩过不少坑，掉过不少头发。现在回头看，其实很多难点只要理清了思路就能迎刃而解。我把项目拆分到了 **`Prj*` 分支** 中，希望这份代码和笔记能给正在熬夜 Debug 的你一点灵感和指引。如果对你有所启发，记得 star 一下。

> **⚠️ Note：** 代码仅供参考，思路比实现更重要。请务必自己动手写，直接 Copy 不仅过不了查重，也会失去这门硬核课程最大的乐趣（和痛苦）。

## 🗺️ 学习路线与进度

我是按照以下路线从零构建这个 RISC-V 内核的：

<div align="center">
  <img src="./assets/learning_roadmap.png" width="92%" alt="RISC-V OS 学习路线" />
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

## 📂 代码结构导读 (以 Prj5 为例)

为了让你少走弯路，我整理了各个目录实际是干嘛的：

<details>
  <summary><b>📂 目录结构树 (点击展开)</b></summary>

  <ul>
    <li>
      <details open>
        <summary><code>init/</code> - <b>梦开始的地方</b></summary>
        <ul>
          <li><code>main.c</code>：内核的入口。从这里开始，我把各个子系统一个个唤醒。</li>
        </ul>
      </details>
    </li>
    <li>
      <details>
        <summary><code>arch/</code> - <b>最底层的黑魔法</b></summary>
        <ul>
          <li>这里全是和 RISC-V 硬件打交道的汇编代码。</li>
          <li><code>boot/</code>：第一行代码运行的地方。</li>
          <li><code>kernel/entry.S</code>：<b>极其重要！</b> 所有的异常、中断、系统调用都会经过这里，一定要看懂它是怎么保存和恢复寄存器的。</li>
        </ul>
      </details>
    </li>
    <li>
      <details>
        <summary><code>kernel/</code> - <b>操作系统的灵魂</b></summary>
        <ul>
          <li><code>sched/</code>：<b>调度器</b> - 这里的代码决定了进程怎么切换，容易出 Bug。</li>
          <li><code>syscall/</code>：<b>系统调用</b> - 也就是 `ecall` 之后发生的事情。</li>
          <li><code>mm/</code>：<b>内存管理</b> - 搞定页表映射，物理地址和虚拟地址的转换全在这。</li>
          <li><code>locking/</code>：<b>锁</b> - 自旋锁、互斥锁，多核的时候要注意。</li>
          <li><code>net/</code>：<b>协议栈</b> - 自己实现的简易 TCP/IP 栈。</li>
        </ul>
      </details>
    </li>
    <li>
      <details>
        <summary><code>drivers/</code> - <b>驱动程序</b></summary>
        <ul>
          <li><b>E1000</b>：网卡驱动。你需要在这里配置繁琐的寄存器和描述符环。</li>
        </ul>
      </details>
    </li>
    <li>
      <details>
        <summary><code>tools/</code> - <b>构建工具</b></summary>
        <ul>
          <li><code>createimage.c</code>：这是我自己写的打包工具，把 Bootblock 和 Kernel 拼成一个镜像文件。</li>
        </ul>
      </details>
    </li>
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