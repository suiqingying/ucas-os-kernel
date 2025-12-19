<div align="center">
  <img src="./assets/banner.svg" width="100%" alt="UCAS OS Kernel banner" />
</div>

<div align="center">
  <a href="../../tree/Prj1"><img alt="Start Prj1" src="https://img.shields.io/badge/Start-Prj1-7C3AED?style=for-the-badge" /></a>
  <a href="../../tree/Prj4"><img alt="VM Prj4" src="https://img.shields.io/badge/Virtual%20Memory-Prj4-22D3EE?style=for-the-badge" /></a>
  <a href="../../tree/Prj5"><img alt="Net Prj5" src="https://img.shields.io/badge/Driver%20%26%20Network-Prj5-34D399?style=for-the-badge" /></a>
  <a href="../../tree/main"><img alt="Main" src="https://img.shields.io/badge/Current-main-111827?style=for-the-badge" /></a>
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

<div align="center">
  <img src="./assets/architecture_overview.png" width="92%" alt="UCAS-OS 操作系统架构概览：Project0-6" />
  <p><i>架构概览：按 Project0-6 划分的知识与实现模块</i></p>
</div>

---

## 项目分支

> 推荐阅读顺序：Project0（准备）→ `Prj1 → Prj2 → Prj3 → Prj4 → Prj5 → Prj6`（`main` 为当前基线）

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
      <p>提示：以 <code>main</code> 的内容与状态为准。</p>
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
