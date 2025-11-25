#include <os/kernel.h>
#include <os/loader.h>
#include <os/string.h>
#include <os/task.h>
#include <printk.h>
#include <type.h>

#define PIPE_LOC 0x54000000 /* address of pipe */
/* Boot info offsets used by createimage.c */
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define BATCH_FILE_MAX_TASKS 16
#define BATCH_FILE_TASK_NAME_LEN 16

uint64_t load_task_img(char *task_name) {
    // load task via task name, thus the arg should be 'char *taskname'
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (tasks[i].block_nums > 0 && strcmp(task_name, tasks[i].task_name) == 0) {
            uint64_t mem_addr = TASK_MEM_BASE + TASK_SIZE * i;
            int start_sec = tasks[i].start_addr / 512;
            bios_sd_read(TMP_MEM_BASE, tasks[i].block_nums, start_sec);
            memcpy((uint8_t *)(uint64_t)(mem_addr), (uint8_t *)(uint64_t)(TMP_MEM_BASE + (tasks[i].start_addr - start_sec * 512)),
                   tasks[i].block_nums * 512);
            return mem_addr;
        }
    }
    return 0;
}

void do_list() {
    printk("User programs:\n");
    for (int i = 0; i < TASK_MAXNUM; i++) {
        if (strlen(tasks[i].task_name) > 0) {
            printk("%s ", tasks[i].task_name);
        }
    }
    printk("\n");
}