#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#include<os/loader.h>
#include <printk.h>
#define PIPE_LOC 0x54000000 /* address of pipe */
/* Boot info offsets used by createimage.c */
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define APP_INFO_ADDR_LOC (BOOT_LOADER_SIG_OFFSET - 10)
#define BATCH_FILE_MAX_TASKS 16
#define BATCH_FILE_TASK_NAME_LEN 16

uint64_t load_task_img(char *task_name)
{
    // load task via task name, thus the arg should be 'char *taskname'
    for (int i = 0; i < TASK_MAXNUM; i++)
    {
        /* 只考虑有效的已登记任务（block_nums>0），避免加载未使用的 slot */
        if (tasks[i].block_nums > 0 && strcmp(task_name, tasks[i].task_name) == 0)
        {
            uint64_t mem_addr = TASK_MEM_BASE + TASK_SIZE * i;
            int start_sec = tasks[i].start_addr / 512;
            bios_sd_read(mem_addr, tasks[i].block_nums, start_sec);
            return mem_addr + (tasks[i].start_addr - start_sec * 512);
        }
    }
    return 0;
}

void do_list()
{
    printk("User programs:\n");
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (strlen(tasks[i].task_name) > 0){
            printk("%s ", tasks[i].task_name);
        }
    }
    printk("\n");
}