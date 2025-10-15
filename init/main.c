#include <common.h>
#include <asm.h>
#include <os/kernel.h>
#include <os/task.h>
#include <os/string.h>
#include <os/loader.h>
#include <type.h>

#define VERSION_BUF 50

int version = 2; // version must between 0 and 9
char buf[VERSION_BUF];

// Task info array
task_info_t tasks[TASK_MAXNUM];

static int bss_check(void)
{
    for (int i = 0; i < VERSION_BUF; ++i)
    {
        if (buf[i] != 0)
        {
            return 0;
        }
    }
    return 1;
}

static void init_jmptab(void)
{
    volatile long (*(*jmptab))() = (volatile long (*(*))())KERNEL_JMPTAB_BASE;

    jmptab[CONSOLE_PUTSTR]  = (long (*)())port_write;
    jmptab[CONSOLE_PUTCHAR] = (long (*)())port_write_ch;
    jmptab[CONSOLE_GETCHAR] = (long (*)())port_read_ch;
    jmptab[SD_READ]         = (long (*)())sd_read;
    jmptab[SD_WRITE]        = (long (*)())sd_write;
}

static void init_task_info(int app_info_loc, int app_info_size)
{
    // TODO: [p1-task4] Init 'tasks' array via reading app-info sector
    // NOTE: You need to get some related arguments from bootblock first
    int start_sec, blocknums;
    start_sec = app_info_loc / SECTOR_SIZE;
    blocknums = NBYTES2SEC(app_info_loc + app_info_size) - start_sec;
    int task_info_addr = TASK_INFO_MEM;
    bios_sd_read(task_info_addr, blocknums, start_sec);
    int start_addr = (TASK_INFO_MEM + app_info_loc - start_sec * SECTOR_SIZE);
    memcpy((uint8_t *)tasks, (uint8_t *)start_addr, app_info_size);
}

/************************************************************/
/* Do not touch this comment. Reserved for future projects. */
/************************************************************/

int main(int app_info_loc, int app_info_size)
{
    // Check whether .bss section is set to zero
    int check = bss_check();

    // Init jump table provided by kernel and bios(ΦωΦ)
    init_jmptab();

    // Init task information (〃'▽'〃)
    init_task_info(app_info_loc, app_info_size);

    // Output 'Hello OS!', bss check result and OS version
    char output_str[] = "bss check: _ version: _\n\r";
    char output_val[2] = {0};
    int i, output_val_pos = 0;

    output_val[0] = check ? 't' : 'f';
    output_val[1] = version + '0';
    for (i = 0; i < sizeof(output_str); ++i)
    {
        buf[i] = output_str[i];
        if (buf[i] == '_')
        {
            buf[i] = output_val[output_val_pos++];
        }
    }

    bios_putstr("Hello OS!\n\r");
    bios_putstr(buf);

    while (1) {
        bios_putstr("Please input task name:\n");

        char input_name[16] = "";
        int idx = 0;
        int ch;
        // 读取字符串直到回车
        while ((ch = bios_getchar()) != '\r' && idx < 31) {
            if (ch != -1) {
                input_name[idx++] = (char)ch;
                bios_putchar(ch); // 回显
            }
        }
        input_name[idx] = '\0';

        uint64_t entry_addr = load_task_img(input_name);
        bios_putstr("\n");
        if(entry_addr != 0){
            void (*entry)(void) = (void (*)(void))entry_addr;
            entry();
            bios_putstr("Task loaded. You can input again.\n\r");
        }
    }

    // [p1-task3] 交互式输入 task id 并运行对应用户程序
    // int tasknum = TASK_MAXNUM; // 或者根据实际任务数赋值
    // bios_putstr("Input task id to run (0 ~ ");
    // bios_putchar('0' + tasknum - 1); // 假设 tasknum 已知
    // bios_putstr("):\n\r");

    // int input_id = 0;
    // while (1) {
    //     bios_putstr("task id> ");
    //     input_id = 0;
    //     int ch;
    //     // 读取数字并回显
    //     while (1) {
    //         ch = bios_getchar();
    //         if (ch == '\n' || ch == '\r') break;
    //         if (ch >= '0' && ch <= '9') {
    //             bios_putchar(ch);
    //             input_id = input_id * 10 + (ch - '0');
    //         }
    //     }
    //     bios_putstr("\n\r");

    //     // 检查合法性
    //     if (input_id < 0 || input_id >= tasknum) {
    //         bios_putstr("Invalid task id!\n\r");
    //         continue;
    //     }

    //     // 加载并执行
    //     uint64_t entry_addr = load_task_img(input_id);
    //     bios_putstr("Loading task...\n\r");

    //     // 用函数指针跳转到用户程序入口
    //     void (*entry)(void) = (void (*)(void))entry_addr;
    //     entry();

    //     bios_putstr("Task loaded. You can input again.\n\r");
    // }
        
    // [p1-task2] 持续读取键盘输入并回显到屏幕
    // bios_putstr("Input from keyboard (echo):\n\r");
    // while (1)
    // {
    //     int ch = bios_getchar();   // 跳转表API读取键盘
    //     if (ch != -1)              // 只处理有效输入
    //         bios_putchar(ch);      // 跳转表API回显
    // }

    // TODO: Load tasks by either task id [p1-task3] or task name [p1-task4],
    //   and then execute them.

    // Infinite while loop, where CPU stays in a low-power state (QAQQQQQQQQQQQ)
    while (1)
    {
        asm volatile("wfi");
    }

    return 0;
}
