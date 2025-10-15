#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#define PIPE_LOC 0x54000000 /* address of pipe */
void batch_process(int initial_value); // 提前声明
void list_user_tasks();

/* uint64_t load_task_img(int taskid)
{ [p1-task3]
    /*
     * TODO:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     * /

    // 计算用户程序在内存中的加载地址
    uint64_t entry_addr = TASK_MEM_BASE + taskid * 15 * 512;
    // 计算用户程序在镜像中的起始扇区
    int img_sector = 1 + taskid * 15;

    // 从镜像读入内存
    bios_sd_read(entry_addr, 15, img_sector);

    // 返回内存中的入口地址
    return entry_addr;
 } */

 uint64_t load_task_img(char *task_name)
{
    /**
     * TODO:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */
    uint64_t mem_addr;
    int start_sec;
    if (strcmp(task_name, "batch") == 0) {
        batch_process(0); // 批处理任务，初始值为0
        return 0; // 批处理任务不需要返回入口地址
    }
    if (strcmp(task_name, "list") == 0) {
        list_user_tasks(); // 列出用户程序
        return 0; // 列出任务不需要返回入口地址
    }
    if (strlen(task_name) == 0) {
        bios_putstr("No task name entered. Please try again.\n\r");
        return 0; // 没有输入任务名，返回0
    }
    for (int i = 0; i < TASK_MAXNUM; i++)
    {
        if (strcmp(task_name, tasks[i].task_name) == 0)
        {
            mem_addr = TASK_MEM_BASE + TASK_SIZE * i;
            start_sec = tasks[i].start_addr / 512; // 起始扇区：向下取整
            bios_sd_read(mem_addr, tasks[i].block_nums, start_sec);

            return mem_addr + (tasks[i].start_addr - start_sec * 512); // 返回程序存储的起始位置
        }
    }
    // 匹配失败，提醒重新输入
    bios_putchar('\n');
    char *output_str = "Fail to find the task! Please try again!";
    for (int i = 0; i < strlen(output_str); i++)
    {
        bios_putchar(output_str[i]);
    }
    bios_putchar('\n');
    return 0;
}

void batch_process(int initial_value)
{
    int *pipe = (int *)PIPE_LOC;
    *pipe = initial_value;

    // 依次执行四个用户程序
    char *tasks[] = {"number", "add10", "mul3", "square"};
    int task_count = 4;

    for (int i = 0; i < task_count; i++) {
        uint64_t entry_addr = load_task_img(tasks[i]);
        bios_putstr("\n");
        if (entry_addr != 0) {
            void (*entry)(void) = (void (*)(void))entry_addr;
            entry();
            bios_putstr("Task finished.\n\r");
        } else {
            bios_putstr("Task not found!\n\r");
        }
    }
}

void list_user_tasks()
{
    bios_putstr("User programs:\n\r");
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (strlen(tasks[i].task_name) > 0) {
            bios_putstr(tasks[i].task_name);
            bios_putstr("\n\r");
        }
    }
}
