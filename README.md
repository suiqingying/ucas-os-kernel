# Pro1: 国科大操作系统研讨课 Project 1（RISC-V 版本）

## 内存空间布局

| 地址         | 说明             |
| ------------ | ---------------- |
| 0x50200000   | bootloader       |
| 0x50201000   | kernel           |
| 0x50500000   | kernel_stack     |
| 0x52000000   | 用户程序0        |
| ...          | ...              |
| 0x520f0000   | 用户程序15（最多16个） |
| 0x52300000   | 用户程序info数组地址 |
| 0x52500000   | 用户程序栈       |

---

## 任务1：制作第一个引导块

- 在 `bootblock.S` 中调用 BIOS_PUTSTR 打印字符串：

```assembly
la a0, msg
li a7, BIOS_PUTSTR
jal bios_func_entry
```

---

## 任务2：加载和初始化内存

- 使用 BIOS_SDREAD 从 SD 卡加载内核：

```assembly
li a7, BIOS_SDREAD
la a0, kernel
la t0, os_size_loc
lh a1, 0(t0)
li a2, 1
jal bios_func_entry
```

- 内核占用扇区数由 createimage 写入 bootblock 最后 4 字节（0x502001fc），用 `lh` 指令读取。

- 清空 BSS 段：

```assembly
la t0, __bss_start
la t1, __BSS_END__
do_clear:
  sw zero, 0(t0)
  addi t0, t0, 4
  bltu t0, t1, do_clear
```

- 键盘输入与回显，注意过滤 -1：

```c
int tmp;
while(1){
    while((tmp=bios_getchar())==-1);
    bios_putchar(tmp);
}
```

---

## 任务3：加载并选择启动多个用户程序之一

- 镜像制作工具 createimage 使用 `fseek` 和 `fwrite` 定位和写入：

```c
int nsec_kern = NBYTES2SEC(nbytes_kernel);
fseek(img, OS_SIZE_LOC, SEEK_SET);  
fwrite(&nsec_kern, 2, 1, img);      
printf("Kernel size: %d sectors\n", nsec_kern);
```

- 固定分区：bootloader 占 1 扇区，kernel 和每个应用占 15 扇区，使用 `write_padding` 填充。

- main.c 交互式选择 task_id，加载并运行：

```c
int taskid;
uint64_t entry_addr;
void (*entry) (void);
while(1){
    while((taskid=bios_getchar())==-1);
    bios_putchar(taskid);
    taskid -= '0';
    if(taskid>=0 && taskid<=TASK_MAXNUM){
        bios_putchar('\n');
        entry_addr = load_task_img(taskid);
        entry = (void*) entry_addr;
        entry();
    }
}
```

- `load_task_img` 通过 taskid 计算磁盘位置，加载到内存：

```c
uint64_t entry_addr;
char info[] = "Loading task _ ...\n\r";
for(int i=0;i<strlen(info);i++){
    if(info[i]!='_') bios_putchar(info[i]);
    else bios_putchar(taskid +'0');
}
entry_addr = TASK_MEM_BASE + TASK_SIZE * (taskid - 1);  
bios_sd_read(entry_addr, 15, 1 + taskid * 15);
return entry_addr;
```

- crt0.S 负责用户程序 C 运行环境初始化，注意返回内核栈。

---

## 任务4: 镜像文件的紧密排列

- createimage.c 不再为 kernel 和应用程序 padding，所有内容紧密排列。

- 定义任务信息结构体：

```c
typedef struct {
    char task_name[16];
    int start_addr;
    int block_nums;
} task_info_t;
```

- task_info_t 数组写入 image 末尾，位置和大小记录在 bootblock 末尾：

```c
int info_size = sizeof(task_info_t) * tasknum;
fseek(img, APP_INFO_ADDR_LOC, SEEK_SET);
fwrite(taskinfo_addr, 4, 1, img);
fwrite(&info_size, 4, 1, img);
fseek(img, *taskinfo_addr, SEEK_SET);
fwrite(taskinfo, sizeof(task_info_t), tasknum, img);
```

- main.c 初始化任务信息：

```c
static void init_task_info(int app_info_loc, int app_info_size)
{
    int start_sec, blocknums;
    start_sec = app_info_loc / SECTOR_SIZE;
    blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sec;
    int task_info_addr = TASK_INFO_MEM;
    bios_sd_read(task_info_addr, blocknums, start_sec);
    int start_addr = (TASK_INFO_MEM + app_info_loc - start_sec * SECTOR_SIZE);
    uint8_t *tmp = (uint8_t *)(start_addr);
    memcpy((uint8_t *)tasks, tmp, app_info_size);
}
```

- `load_task_img` 支持按名字加载：

```c
uint64_t load_task_img(char *taskname){
    int i;
    int entry_addr;
    int start_sec;
    for(i=0;i<TASK_MAXNUM;i++){
        if(strcmp(taskname, tasks[i].task_name)==0){
            entry_addr = TASK_MEM_BASE + TASK_SIZE * i;
            start_sec = tasks[i].start_addr / 512;
            bios_sd_read(entry_addr, tasks[i].block_nums, start_sec);
            return entry_addr + (tasks[i].start_addr - start_sec*512);
        }
    }
    char *output_str = "Fail to find the task! Please try again!";
    for(i=0; i<strlen(output_str); i++){
        bios_putchar(output_str[i]);
    }
    bios_putchar('\n');
    return 0;
}
```

- crt0.S 修正用户栈返回，O2 编译通过。

---

## 任务5：批处理运行多个用户程序和管道输入

- 使用 0x54000000 作为管道地址，用户程序输出/输入均通过该地址。

- 输入 `batch` 进入批处理，顺序加载并运行各应用。

- 批处理逻辑：定义 batch 函数，依次加载运行各应用，数据通过管道传递。

---

## 编译与运行

1. 创建 build 目录：`make dirs`
2. 编译所有文件：`make all`
3. 制作镜像文件：`cd build && ./createimage --extended bootblock main ...`
4. 运行 QEMU：`make run`
5. （可选）写入 SD 卡：`make floppy`

---

## 参考资料

- 课程任务书
- RISC-V 指令集手册
- Linux ELF 文件格式文档

---

如有疑问或建议，请联系作者。