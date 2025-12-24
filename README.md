# Project 6：文件系统开发全攻略

本指南旨在为国科大操作系统研讨课 Project 6 提供清晰的实现路线，覆盖盘上布局、目录操作与后续扩展的基础约束。

## 一、核心理论：盘上布局与元数据

### 1. Superblock：全局配置中心

- 记录文件系统总大小、block 大小、bitmap/inode/data 区起始位置
- 内核启动时通过 magic 校验，决定是否需要自动格式化

### 2. 位图：空间分配的源头

- inode bitmap：标记 inode 是否被占用
- block bitmap：标记数据块是否被占用
- 分配策略：线性扫描即可满足实验规模

### 3. inode 与 dentry：文件与目录的“身份证”

- inode 保存类型、大小、链接数、数据块索引
- dentry 保存名称、inode 号与类型
- 目录即 dentry 的序列化数组

### 4. 数据区：真正的文件内容

- 目录数据也是普通数据块，只是内容解释为 dentry
- 文件数据按 block 顺序存储与读写

## 二、基础设施：块、位图与路径解析

### 1. 块与扇区换算

- 统一采用 4KB block，按 `block_id * SECTORS_PER_BLOCK` 定位扇区
- SD 读写地址必须为物理地址

### 2. 目录项扫描与复用

- 读取目录文件，按固定 dentry 大小逐项扫描
- 删除目录项时仅清空 inode/名字，保留空槽供复用

### 3. 路径解析

- 支持相对路径与绝对路径
- 识别 `.` 与 `..`，多级目录逐层解析

## 三、任务一：物理文件系统（Task 1）

### 1. mkfs 初始化

- 初始化 superblock、两张 bitmap 与 inode table
- 创建根目录并写入 `.` / `..`
- 建议打印布局信息便于调试

### 2. statfs

- 读取 bitmap 统计 inode/block 使用量
- 输出总量/已用/剩余

### 3. mkdir / rmdir

- mkdir：分配 inode + 数据块 + dentry，写入 `.` / `..`
- rmdir：只允许空目录删除（仅 `.`/`..`）

### 4. cd / ls

- cd 更新当前进程 cwd inode
- ls 输出目录项；`-l` 打印 inode/链接数/大小

### 5. 注意事项

- 目录操作以相对路径为主，不要求复杂权限
- mkfs 后需保证元数据持久化到 SD


## 四、任务二：文件操作与大文件（Task 2）

### 1. 文件描述符 (fd) 语义

- open 返回 fd，记录 inode/offset/权限
- read/write 根据 offset 前进，close 释放 fd

### 2. open / read / write / close

- read 越界返回 0，不修改 offset
- write 扩容时更新 inode size
- 写入需及时落盘保证持久化

### 3. touch / cat / ln / rm

- touch：通过写模式 open 创建空文件
- cat：顺序 read 并打印
- ln：增加硬链接计数
- rm：仅删除 dentry；链接数归零才释放 inode 与数据块

### 4. 大文件与 lseek

- 采用双层间接索引扩展寻址空间
- 支持 SEEK_SET / SEEK_CUR / SEEK_END
- 支持空洞文件，lseek 可跳过中间块


## 五、任务三：缓存与 /proc/sys/vm（Task 3）

### 1. 缓存结构

- 以 block 为粒度缓存数据与元数据
- cache entry 记录 block_id 与 dirty 标记

### 2. write back / write through

- write back：写入缓存，按频率批量刷盘
- write through：写入缓存同时落盘
- 从 write back 切换到 write through 时需立即刷盘

### 3. /proc/sys/vm

- 默认文件内容：
  ```ini
  page_cache_policy = write back
  write_back_freq = 30
  ```
- 修改 vm 文件后即时生效

### 4. 测试建议

- 读缓存：`exec cache_read init 32` 生成 32MB 文件，重启后 `exec cache_read` 对比 read-1/read-2 ticks
- 元数据缓存：`exec meta_bench 5000` 创建/查找 5000 文件，对比 lookup-1/lookup-2 ticks
- 写策略：`vm wb 30` 后运行 `exec write_policy 8` 立即断电，再用 `vm wt` 重复并对比持久化结果
