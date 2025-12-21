# UCAS OS Kernel 实习指导书 🎓

> "The best way to learn an operating system is to write one."

[![GitHub Pages](https://img.shields.io/badge/GitHub%20Pages-Live-success?style=for-the-badge&logo=github)](https://suiqingying.github.io/ucas-os-kernel/)
[![License](https://img.shields.io/badge/license-MIT-blue.svg?style=for-the-badge)](LICENSE)

**[👉 点击立即开始阅读在线文档 👈](https://suiqingying.github.io/UCAS-OSLab-Kernel/)**

---

这是一个为中国科学院大学（UCAS）操作系统研讨课设计的参考内核实现与学习指南。不同于枯燥的代码仓库，我们为你精心准备了一份**交互式在线电子书**。

## 📚 项目亮点

*   **交互式学习路线**：从环境搭建到文件系统，七大阶段一目了然。
*   **沉浸式阅读体验**：采用 Nord 护眼配色，像阅读技术书籍一样阅读代码文档。
*   **代码架构全景**：不再迷失在文件夹中，核心模块一目了然。

### 学习路线图
![Learning Roadmap](docs/assets/learning_roadmap.png)

### 内核架构概览
![Architecture Overview](docs/assets/architecture_overview.png)

## 🛠️ 内容概览

| 阶段 | 核心任务 | 关键词 |
| :--- | :--- | :--- |
| **Prj0** | 环境准备 | QEMU, GDB, OpenSBI |
| **Prj1** | 引导与加载 | Bootloader, ELF, Linker Script |
| **Prj2** | 内核核心机制 | Context Switch, Trap Handling, Lock |
| **Prj3** | 进程与调度 | PCB, Round-Robin, Priority |
| **Prj4** | 虚拟内存 | Sv39 Page Table, TLB, Page Fault |
| **Prj5** | 网络驱动 | E1000 Driver, DMA, TCP/IP Stack |
| **Prj6** | 文件系统 | inode, Directory, File I/O |

## 🚀 快速开始

不要直接阅读 Markdown 源码！请访问我们构建好的在线文档，获得最佳体验：

**[https://suiqingying.github.io/UCAS-OSLab-Kernel/](https://suiqingying.github.io/UCAS-OSLab-Kernel/)**

## 🤝 贡献与反馈

如果你发现了文档中的错误，或者有更好的实现思路，欢迎提交 Issue 或 Pull Request。

Give a ⭐️ if this project helped you!
