<div align="center">
  <img src="./assets/architecture_overview.png" width="100%" alt="UCAS-OS 操作系统架构概览（Project0-6）" />
</div>

<div align="center">
  <a href="../../tree/Prj1"><img alt="Start Prj1" src="https://img.shields.io/badge/Start-Prj1-7C3AED?style=for-the-badge" /></a>
  <a href="../../tree/Prj4"><img alt="VM Prj4" src="https://img.shields.io/badge/Virtual%20Memory-Prj4-22D3EE?style=for-the-badge" /></a>
  <a href="../../tree/Prj5"><img alt="Net Prj5" src="https://img.shields.io/badge/Driver%20%26%20Network-Prj5-34D399?style=for-the-badge" /></a>
</div>

---

## 这是什么

这个仓库的**内容不集中在一个分支**：每个 `Prj*` 分支对应一个项目阶段/主题，并各自包含完整的文档与实现记录；本分支用于把这些内容“摆上台面”——让人一眼知道该从哪里读、读什么、怎么跳转。

## 你会在这里看到什么

<table>
  <tr>
    <td width="25%"><b>Boot & Image</b><br/>从 Bootloader 到镜像组织、ELF 装载。</td>
    <td width="25%"><b>Kernel Core</b><br/>调度、锁、系统调用、中断与抢占。</td>
    <td width="25%"><b>Virtual Memory</b><br/>Sv39、缺页、Swap、内存统计与调试。</td>
    <td width="25%"><b>Drivers & Net</b><br/>E1000、收发/中断、可靠传输实验。</td>
  </tr>
</table>

<div align="center">
  <img src="./assets/learning_roadmap.png" width="92%" alt="从零开始构建 RISC-V 操作系统：学习路线图" />
  <p><i>学习路线：从启动与引导 → 核心功能 → 虚拟内存与驱动 → 文件系统</i></p>
</div>

---

## 目录结构（以 `Prj5` 为基准，可展开）

<p>下面是一个“可点击/可伸缩”的目录树，帮助你快速定位：每个文件夹负责什么、应该从哪里开始读。</p>
<p><b>说明：</b>这是以 <code>Prj5</code> 分支为基准整理的；其他分支可能会有增减。</p>

<details open>
  <summary><b>Prj5 目录树（点击展开/收起）</b></summary>

  <ul>
    <li>
      <details>
        <summary><code>arch/</code> · 启动/陷入/CSR（RISC-V 相关） · <a href="../../tree/Prj5/arch">打开</a></summary>
        <ul>
          <li><code>arch/riscv/boot/</code>：bootloader · <a href="../../blob/Prj5/arch/riscv/boot/bootblock.S">bootblock.S</a></li>
          <li><code>arch/riscv/kernel/</code>：启动与陷入入口 · <a href="../../blob/Prj5/arch/riscv/kernel/trap.S">trap.S</a> · <a href="../../blob/Prj5/arch/riscv/kernel/entry.S">entry.S</a> · <a href="../../blob/Prj5/arch/riscv/kernel/head.S">head.S</a></li>
          <li><code>arch/riscv/include/</code>：CSR/页表/原子等公共定义 · <a href="../../blob/Prj5/arch/riscv/include/csr.h">csr.h</a> · <a href="../../blob/Prj5/arch/riscv/include/pgtable.h">pgtable.h</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>kernel/</code> · 内核核心子系统 · <a href="../../tree/Prj5/kernel">打开</a></summary>
        <ul>
          <li>
            <details>
              <summary><code>kernel/irq/</code>：异常/中断分发 · <a href="../../blob/Prj5/kernel/irq/irq.c">irq.c</a></summary>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/sched/</code>：调度与时间 · <a href="../../blob/Prj5/kernel/sched/sched.c">sched.c</a> · <a href="../../blob/Prj5/kernel/sched/time.c">time.c</a></summary>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/mm/</code>：内存/映射 · <a href="../../blob/Prj5/kernel/mm/mm.c">mm.c</a> · <a href="../../blob/Prj5/kernel/mm/ioremap.c">ioremap.c</a></summary>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/net/</code>：网络栈入口 · <a href="../../blob/Prj5/kernel/net/net.c">net.c</a></summary>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/syscall/</code>：系统调用表与实现 · <a href="../../blob/Prj5/kernel/syscall/syscall.c">syscall.c</a></summary>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/locking/</code>：同步原语 · <a href="../../blob/Prj5/kernel/locking/lock.c">lock.c</a></summary>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/loader/</code>：用户程序加载 · <a href="../../blob/Prj5/kernel/loader/loader.c">loader.c</a></summary>
            </details>
          </li>
          <li><code>kernel/smp/</code>：多核支持 · <a href="../../blob/Prj5/kernel/smp/smp.c">smp.c</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>drivers/</code> · 设备驱动（Prj5 的重点） · <a href="../../tree/Prj5/drivers">打开</a></summary>
        <ul>
          <li><b>E1000</b>：<a href="../../blob/Prj5/drivers/e1000.c">drivers/e1000.c</a> · <a href="../../blob/Prj5/drivers/e1000.h">drivers/e1000.h</a></li>
          <li>PLIC：<a href="../../blob/Prj5/drivers/plic.c">drivers/plic.c</a></li>
          <li>Screen：<a href="../../blob/Prj5/drivers/screen.c">drivers/screen.c</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>include/</code> · 头文件与接口 · <a href="../../tree/Prj5/include">打开</a></summary>
        <ul>
          <li><code>include/os/</code>：内核子系统接口 · <a href="../../blob/Prj5/include/os/net.h">net.h</a> · <a href="../../blob/Prj5/include/os/mm.h">mm.h</a> · <a href="../../blob/Prj5/include/os/sched.h">sched.h</a></li>
          <li><code>include/sys/</code>：系统调用号 · <a href="../../blob/Prj5/include/sys/syscall.h">syscall.h</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>init/</code> · 内核初始化入口 · <a href="../../tree/Prj5/init">打开</a></summary>
        <ul>
          <li><a href="../../blob/Prj5/init/main.c">init/main.c</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>test/</code> · 用户态测试（验证实现） · <a href="../../tree/Prj5/test">打开</a></summary>
        <ul>
          <li><a href="../../blob/Prj5/test/shell.c">test/shell.c</a>：交互 shell</li>
          <li><code>test/test_project5/</code>：网卡/可靠接收测试 · <a href="../../blob/Prj5/test/test_project5/recv_stream.c">recv_stream.c</a> · <a href="../../blob/Prj5/test/test_project5/recv2.c">recv2.c</a> · <a href="../../blob/Prj5/test/test_project5/send.c">send.c</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>tools/</code> · 构建与工具 · <a href="../../tree/Prj5/tools">打开</a></summary>
        <ul>
          <li><a href="../../blob/Prj5/tools/createimage.c">tools/createimage.c</a>：镜像制作</li>
          <li><code>tools/pkt-rx-tx-master/</code>：发包/抓包辅助工具 · <a href="../../tree/Prj5/tools/pkt-rx-tx-master/images">images/</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>tiny_libc/</code> · 用户态 libc 与 syscall 封装 · <a href="../../tree/Prj5/tiny_libc">打开</a></summary>
        <ul>
          <li><a href="../../blob/Prj5/tiny_libc/syscall.c">tiny_libc/syscall.c</a></li>
          <li><a href="../../tree/Prj5/tiny_libc/include">tiny_libc/include/</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>libs/</code> · 内核基础库 · <a href="../../tree/Prj5/libs">打开</a></summary>
        <ul>
          <li><a href="../../blob/Prj5/libs/printk.c">libs/printk.c</a> · <a href="../../blob/Prj5/libs/string.c">libs/string.c</a></li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>guide/</code> · 实验指导书 · <a href="../../tree/Prj5/guide">打开</a></summary>
        <ul>
          <li><a href="../../blob/Prj5/guide/guide_book_p5.pdf">guide_book_p5.pdf</a> · <a href="../../blob/Prj5/guide/guide_book_p6.pdf">guide_book_p6.pdf</a></li>
        </ul>
      </details>
    </li>
  </ul>
</details>

---

## 项目分支

> 推荐阅读顺序：Project0（准备）→ `Prj1 → Prj2 → Prj3 → Prj4 → Prj5 → Prj6`（`Prj5` 为当前基线）

<table>
  <tr>
    <td width="33%">
      <h3>Project 0</h3>
      <p><b>准备工作（环境 & 工具链）</b></p>
      <p>
        <a href="../../tree/main/guide">打开 guide/</a>
      </p>
      <p>适合第一次上手：构建、运行、调试工具的入口。</p>
    </td>
    <td width="33%"></td>
    <td width="33%">
      <h3>Current (Prj5)</h3>
      <p><b>当前工作基线</b></p>
      <p>
        <a href="../../tree/Prj5">进入分支</a> ·
        <a href="../../blob/Prj5/README.md">打开 README</a>
      </p>
    </td>
    
  </tr>
  <tr>
    <td width="33%">
      <h3>Project 1</h3>
      <p><b>引导、镜像、ELF</b></p>
      <p>
        <a href="../../tree/Prj1">进入分支</a> ·
        <a href="../../blob/Prj1/README.md">打开 README</a>
      </p>
      <p>关键词：Boot、createimage、ELF、加载器</p>
    </td>
    <td width="33%">
      <h3>Project 2</h3>
      <p><b>调度、锁、系统调用</b></p>
      <p>
        <a href="../../tree/Prj2">进入分支</a> ·
        <a href="../../blob/Prj2/README.md">打开 README</a>
      </p>
      <p>关键词：PCB、do_scheduler、mutex、ecall、timer IRQ</p>
    </td>
    <td width="33%">
      <h3>Project 3</h3>
      <p><b>综合阶段</b></p>
      <p>
        <a href="../../tree/Prj3">进入分支</a> ·
        <a href="../../blob/Prj3/README.md">打开 README</a>
      </p>
      <p>提示：该分支的 <code>README.md</code> 目前为空（待补充）。</p>
    </td>
  </tr>
  <tr>
    <td width="33%">
      <h3>Project 4</h3>
      <p><b>内存管理与虚拟内存</b></p>
      <p>
        <a href="../../tree/Prj4">进入分支</a> ·
        <a href="../../blob/Prj4/README.md">打开 README</a>
      </p>
      <p>关键词：Sv39、page fault、swap、零拷贝 IPC（页管道）</p>
    </td>
    <td width="33%">
      <h3>Project 5</h3>
      <p><b>网卡驱动与可靠传输</b></p>
      <p>
        <a href="../../tree/Prj5">进入分支</a> ·
        <a href="../../blob/Prj5/README.md">打开 README</a>
      </p>
      <p>关键词：E1000、TX/RX、中断、可靠接收/重传</p>
    </td>
    <td width="33%">
      <h3>Project 6</h3>
      <p><b>文件系统（文件管理）</b></p>
      <p>
        <a href="../../tree/Prj6">进入分支</a> ·
        <a href="../../blob/Prj6/README.md">打开 README</a>
      </p>
      <p>提示：尚未完成 <code>Prj6</code> 分支</p>
    </td>
  </tr>
</table>

---

## 给以后同学的建议（怎么学更省时间）

- 先跑起来再读代码：优先看该分支 `README.md` 的“运行/测试”，确保你能复现输出。
- 每个项目都留一份“你自己的笔记”：把踩坑、关键寄存器/数据结构、关键函数调用链记下来。
- 读代码建议顺序：`arch/`（启动与陷入）→ `kernel/`（核心机制）→ `drivers/`（设备）→ `test/`（验证用例）。

## 快速切换

```bash
git checkout Prj4
```
