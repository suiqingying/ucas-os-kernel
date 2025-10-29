#include <os/task.h>
#include <os/string.h>
#include <os/kernel.h>
#include <type.h>
#define PIPE_LOC 0x54000000 /* address of pipe */
/* Boot info offsets used by createimage.c */
#define BOOT_LOADER_SIG_OFFSET 0x1fe
#define APP_INFO_ADDR_LOC (BOOT_LOADER_SIG_OFFSET - 10)

/* read the batch file sector from boot info (stored at APP_INFO_ADDR_LOC + 8)
 * We cache the value after first read.
 */
static int get_batch_sector_from_bootinfo()
{
    static int cached = -1;
    if (cached != -1) return cached;

    char buf[512] = {0};
    /* read sector 0 which contains the bootblock info */
    sd_read((uintptr_t)buf, 1, 0);
    int value = 0;
    /* batch sector was written as an int at APP_INFO_ADDR_LOC + 8 */
    int offset = APP_INFO_ADDR_LOC + 8;
    /* assemble the int from bytes (little-endian) to avoid memcpy/un-aligned access */
    value = (int)((unsigned char)buf[offset] |
                  ((unsigned char)buf[offset + 1] << 8) |
                  ((unsigned char)buf[offset + 2] << 16) |
                  ((unsigned char)buf[offset + 3] << 24));
    cached = value;
    return cached;
}
#define BATCH_FILE_MAX_TASKS 16
#define BATCH_FILE_TASK_NAME_LEN 16

void batch_process(int initial_value); // 提前声明
void list_user_tasks();
void write_batch_file(char *task_list[], int task_count);
int read_batch_file(char task_list[][BATCH_FILE_TASK_NAME_LEN]);
void batch_process_from_file();
int parse_task_names(char *args, char *task_list[]);
uint64_t load_single_task(char *task_name);
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

 uint64_t load_task_img(char *input_line)
{
    /**
     * TODO:
     * 1. [p1-task3] load task from image via task id, and return its entrypoint
     * 2. [p1-task4] load task via task name, thus the arg should be 'char *taskname'
     */
    char cmd[32] = {0};
    char args[128] = {0};
    int i = 0;
    // 提取命令
    while (input_line[i] != ' ' && input_line[i] != '\0') {
        cmd[i] = input_line[i];
        i++;
    }
    cmd[i] = '\0';
    // 提取参数
    if (input_line[i] == ' ') {
        strncpy(args, input_line + i + 1, sizeof(args) - 1);
    }

    if (strcmp(cmd, "batch_write") == 0) {
        char *task_list[BATCH_FILE_MAX_TASKS];
        int task_count = parse_task_names(args, task_list);
        write_batch_file(task_list, task_count);
        return 0;
    }
    
    if (strcmp(cmd, "batch_run") == 0) {
        batch_process_from_file();
        return 0;
    }

    if (strcmp(cmd, "list") == 0) {
        list_user_tasks(); // 列出用户程序
        return 0; // 列出任务不需要返回入口地址
    }
    // 普通任务名
    return load_single_task(cmd);
}

// void batch_process(int initial_value)
// {
//     int *pipe = (int *)PIPE_LOC;
//     *pipe = initial_value;

//     // 依次执行四个用户程序
//     char *tasks[] = {"number", "add10", "mul3", "square"};
//     int task_count = 4;

//     for (int i = 0; i < task_count; i++) {
//         uint64_t entry_addr = load_task_img(tasks[i]);
//         bios_putstr("\n");
//         if (entry_addr != 0) {
//             void (*entry)(void) = (void (*)(void))entry_addr;
//             entry();
//             bios_putstr("Task finished.\n\r");
//         } else {
//             bios_putstr("Task not found!\n\r");
//         }
//     }
// }

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

// 写入批处理文件
void write_batch_file(char *task_list[], int task_count) {
    char buffer[BATCH_FILE_MAX_TASKS * BATCH_FILE_TASK_NAME_LEN] = {0};
    for (int i = 0; i < task_count && i < BATCH_FILE_MAX_TASKS; i++) {
        strncpy(buffer + i * BATCH_FILE_TASK_NAME_LEN, task_list[i], BATCH_FILE_TASK_NAME_LEN - 1);
    }
    int batch_sector = get_batch_sector_from_bootinfo();
    if (batch_sector <= 0) {
        bios_putstr("\n\rError: batch sector not found in boot info.\n\r");
        return;
    }
    sd_write((uintptr_t)buffer, 1, batch_sector);
    bios_putstr("\n\rBatch file written.\n\r");
}

// 读取批处理文件
int read_batch_file(char task_list[][BATCH_FILE_TASK_NAME_LEN]) {
    char buffer[512] = {0};
    int batch_sector = get_batch_sector_from_bootinfo();
    if (batch_sector <= 0) return 0;
    sd_read((uintptr_t)buffer, 1, batch_sector);
    int count = 0;
    for (int i = 0; i < BATCH_FILE_MAX_TASKS; i++) {
        if (buffer[i * BATCH_FILE_TASK_NAME_LEN] != 0) {
            strncpy(task_list[count], buffer + i * BATCH_FILE_TASK_NAME_LEN, BATCH_FILE_TASK_NAME_LEN);
            count++;
        }
    }
    return count;
}

uint64_t load_single_task(char *task_name)
{
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
    bios_putstr("\nFail to find the task! Please try again!\n");
    return 0;
}

void batch_process_from_file() {
    char task_list[BATCH_FILE_MAX_TASKS][BATCH_FILE_TASK_NAME_LEN] = {0};
    int task_count = read_batch_file(task_list);
    bios_putstr("Batch executing:\n\r");
    for (int i = 0; i < task_count; i++) {
        bios_putstr("Running: ");
        bios_putstr(task_list[i]);
        bios_putstr("\n\r");
        uint64_t entry_addr = load_single_task(task_list[i]);
        if (entry_addr != 0) {
            void (*entry)(void) = (void (*)(void))entry_addr;
            entry();
            bios_putstr("Task finished.\n\r");
        } else {
            bios_putstr("Task not found!\n\r");
        }
    }
}

int parse_task_names(char *args, char *task_list[]) {
    int count = 0;
    char *p = args;
    while (*p && count < BATCH_FILE_MAX_TASKS) {
        // 跳过前导空格
        while (*p == ' ') p++;
        if (!*p) break;
        // 记录任务名起始
        task_list[count++] = p;
        // 找到下一个空格并替换为 '\0'
        while (*p && *p != ' ') p++;
        if (*p) {
            *p = '\0';
            p++;
        }
    }
    return count;
}