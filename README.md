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

<p>下面是一棵“可展开”的结构树：用来解释每个文件夹/模块的职责，以及它在整个系统里扮演的角色（不含超链接）。</p>
<p><b>说明：</b>内容以 <code>Prj5</code> 的目录为基准；其他分支可能会有增减或重构。</p>

<details open>
  <summary><b>结构树（点击展开/收起）</b></summary>

  <ul>
    <li>
      <details open>
        <summary><code>init/</code>：系统启动后，负责把各子系统初始化并串起来</summary>
        <ul>
          <li>典型入口：<code>init/main.c</code></li>
          <li>你可以把它理解为“内核的 main()”：决定先初始化什么，再启动什么。</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>arch/</code>：RISC-V 架构相关（启动、陷入、CSR、页表基础）</summary>
        <ul>
          <li><code>arch/riscv/boot/</code>：引导与加载阶段（bootloader 相关）</li>
          <li><code>arch/riscv/kernel/</code>：早期启动、陷入入口（trap/entry/head 等汇编）</li>
          <li><code>arch/riscv/include/</code>：CSR / 页表 / 原子操作等公共定义</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>kernel/</code>：内核核心子系统（“操作系统能力”主要在这里）</summary>
        <ul>
          <li>
            <details>
              <summary><code>kernel/irq/</code>：异常/中断分发（从 trap 进来后怎么路由）</summary>
              <ul>
                <li>关键问题：谁触发了中断？为什么触发？处理完如何返回？</li>
              </ul>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/sched/</code>：调度与时间（让 CPU 在多个任务之间切换）</summary>
              <ul>
                <li>关键问题：什么时候切换？切到谁？如何保存/恢复上下文？</li>
              </ul>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/syscall/</code>：系统调用（用户态进入内核的 API）</summary>
              <ul>
                <li>关键问题：系统调用号如何分发到具体内核函数？参数怎么传？返回值怎么回？</li>
              </ul>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/mm/</code>：内存管理与映射（页表、分配、MMIO 映射等）</summary>
              <ul>
                <li>关键问题：页表怎么建？物理页怎么分配/回收？设备寄存器怎么映射？</li>
              </ul>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/locking/</code>：同步原语（锁、阻塞/唤醒相关）</summary>
              <ul>
                <li>关键问题：并发访问怎么保护？阻塞队列如何避免丢唤醒？</li>
              </ul>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/loader/</code>：用户程序加载（从镜像/存储读入并启动）</summary>
              <ul>
                <li>关键问题：用户程序放在哪里？入口地址怎么找？加载后如何跳转执行？</li>
              </ul>
            </details>
          </li>
          <li>
            <details>
              <summary><code>kernel/net/</code>：网络相关（收发路径、可靠接收等）</summary>
              <ul>
                <li>关键问题：包从驱动到协议栈怎么走？可靠机制如何处理丢包/乱序？</li>
              </ul>
            </details>
          </li>
          <li><code>kernel/smp/</code>：多核支持（多核启动与同步相关）</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>drivers/</code>：设备驱动（把硬件能力变成可用接口）</summary>
        <ul>
          <li><b>E1000</b>：网卡驱动（TX/RX 描述符环、DMA、寄存器、中断）</li>
          <li><b>PLIC</b>：外部中断控制器（claim/complete）</li>
          <li><b>Screen</b>：输出/显示相关支持</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>include/</code>：头文件与接口（各子系统对外“说话的方式”）</summary>
        <ul>
          <li><code>include/os/</code>：内核子系统接口声明（mm/sched/net/lock...）</li>
          <li><code>include/sys/</code>：系统调用号与约定</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>tiny_libc/</code>：用户态小型 libc + syscall 封装（写测试程序会用到）</summary>
        <ul>
          <li>你可以把它理解为：用户程序“能用 printf/调用 syscall”的最低配支持。</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>test/</code>：用户程序与测试（验证实现、复现现象）</summary>
        <ul>
          <li><code>test/shell.c</code>：交互入口（运行其它测试/程序）</li>
          <li><code>test/test_project5/</code>：网卡与可靠接收相关测试</li>
          <li><code>test/test_project*</code>：不同阶段的测试用例集合</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>tools/</code>：构建与工具（镜像制作、发包工具等）</summary>
        <ul>
          <li><code>tools/createimage.c</code>：镜像制作工具源码</li>
          <li><code>tools/pkt-rx-tx-master/</code>：发包/抓包辅助工具与说明</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>libs/</code>：内核基础库（printk/字符串等）</summary>
        <ul>
          <li>内核里很多“看起来像 libc”的能力会放在这里。</li>
        </ul>
      </details>
    </li>

    <li>
      <details>
        <summary><code>guide/</code>：实验指导书（对照任务要求、验收点）</summary>
        <ul>
          <li>建议：先读对应 project 的 guide，再回头读实现分支的 README 与代码。</li>
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
