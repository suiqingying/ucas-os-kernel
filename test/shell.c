/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *            Copyright (C) 2018 Institute of Computing Technology, CAS
 *               Author : Han Shukai (email : hanshukai@ict.ac.cn)
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *                  The shell acts as a task running in user mode.
 *       The main function is to make system calls through the user's output.
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *  * * * * * * * * * * */

#include <ctype.h>
// #include <os/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define SHELL_BEGIN 40
#define MAX_ARG_NUM 16
#define MAX_ARG_LEN 16
#define MAX_BUFF_LEN 256

char argv[MAX_ARG_NUM][MAX_ARG_LEN];
char buff[MAX_BUFF_LEN];

int parse_arg(const char *buff) {
    int argc = 0;
    for (int i = 0; i < MAX_ARG_NUM; i++) {
        argv[i][0] = '\0';
    }
    while (*buff) {
        while (isspace(*buff)) buff++;
        if (*buff == '\0') break;
        if (argc >= MAX_ARG_NUM) break;
        int i = 0;
        while (!isspace(*buff) && *buff != '\0' && i < MAX_ARG_LEN - 1) {
            argv[argc][i++] = *buff++;
        }
        argv[argc][i] = '\0'; // 考虑到参数结尾不一定有结束符号,这里必须手动添加
        argc++;
    }
    return argc;
}
int main(void) {
    sys_move_cursor(0, SHELL_BEGIN);
    printf("------------------- COMMAND -------------------\n");
    printf("> root@UCAS_OS: ");

    int argc, pos = 0;
    while (1) {
        // call syscall to read UART port
        int ch, end = 0;
        while ((ch = sys_getchar()) == -1);
        // parse input
        // note: backspace maybe 8('\b') or 127(delete)
        if (ch == '\n' || ch == '\r') {
            sys_write_ch('\n');
            sys_reflush();
            end = 1;
            buff[pos] = '\0';
        } else if (ch == 8 || ch == 127) {
            if (pos > 0) {
                sys_write_ch(ch);
                sys_reflush();
                buff[--pos] = '\0';
            }
        } else {
            sys_write_ch(ch);
            sys_reflush();
            buff[pos++] = ch;
        }
        if (end == 0) continue;
        argc = parse_arg(buff);
        pos = 0;
        // ps, exec, kill, clear
        if (strcmp(argv[0], "ps") == 0 && argc == 1) {
            sys_ps();
        } else if (strcmp(argv[0], "clear") == 0 && argc == 1) {
            sys_clear();
            sys_move_cursor(0, SHELL_BEGIN);
            printf("------------------- COMMAND -------------------\n");
        } else if (strcmp(argv[0], "exec") == 0) {
            int no_wait = !strcmp(argv[argc - 1], "&");
            int exec_argc = argc - no_wait - 1;
            char *exec_argv[MAX_ARG_NUM];
            for (int i = 0; i < exec_argc; i++) exec_argv[i] = argv[i + 1];
            pid_t pid = sys_exec(argv[1], exec_argc, exec_argv);
            if (pid == 0) {
                printf("Error: exec failed!\n");
            } else {
                printf("Info: excute %s successfully, pid = %d\n", argv[1], pid);
                if (!no_wait) sys_waitpid(pid);
            }
        } else if (strcmp("tasks", argv[0]) == 0) {
            sys_list();
        } else if (strcmp("kill", argv[0]) == 0) {
            int pid = atoi(argv[1]);
            if (pid == 1) printf("Info: We should not kill the shell process!\n");
            else if (sys_kill(pid) == 0)
                printf("Info: Cannot find process with pid %d!\n", pid);
            else
                printf("Info: kill process %d successfully.\n", pid);
        } else if (strcmp("wait", argv[0]) == 0) {
            int pid = atoi(argv[1]);
            if (sys_waitpid(pid) == 0)
                printf("Info: Cannot find process with pid %d!\n", pid);
            else
                printf("Info: Excute waitpid successfully, pid = %d.\n", pid);
        } else if (strcmp(argv[0], "taskset") == 0) {
            // 解析 taskset 命令
            // 格式 1: taskset [mask] [command] [args...]
            // 格式 2: taskset -p [mask] [pid]

            if (argc < 3) {
                printf("Error: taskset missing arguments\n");
                continue;
            }

            int is_p_flag = (strcmp(argv[1], "-p") == 0);

            if (is_p_flag) {
                // 格式 2: taskset -p [mask] [pid]
                if (argc != 4) {
                    printf("Error: taskset -p [mask] [pid]\n");
                } else {
                    int mask = atoi(argv[2]); // 注意：atoi 只能解十进制，如果是十六进制输入需自己处理
                    if  (mask == 0) {
                        printf("Error: Invalid mask 0\n");
                        continue;
                    }
                    int pid = atoi(argv[3]);
                    sys_taskset(pid, mask);
                    printf("Info: Set process %d affinity to 0x%x\n", pid, mask);
                }

            } else {
                // 格式 1: taskset [mask] [command]
                int mask = atoi(argv[1]); // atoi 可以自动判断十进制或十六进制格式

                if (mask == 0) {
                    printf("Error: Invalid mask 0\n");
                    continue;
                }
                // 1. 构建 exec 的参数
                int exec_argc = argc - 2;
                char *exec_argv[MAX_ARG_NUM];
                for (int i = 0; i < exec_argc; i++) {
                    exec_argv[i] = argv[i + 2];
                }

                // 2. 临时修改 Shell 自己的 mask
                pid_t shell_pid = sys_getpid();
                sys_taskset(shell_pid, mask);

                // 3. 启动新进程 (它会继承 Shell 当前的 mask)
                pid_t new_pid = sys_exec(exec_argv[0], exec_argc, exec_argv);

                // 4. 恢复 Shell 的 mask (恢复为允许所有核 0x3)
                sys_taskset(shell_pid, 0x3);

                if (new_pid == 0) {
                    printf("Error: exec failed!\n");
                } else {
                    printf("Info: taskset execute %s with mask 0x%x successfully, pid = %d\n", exec_argv[0], mask, new_pid);
                    // sys_waitpid(new_pid);
                }
            }
        } else if (strcmp(argv[0], "free") == 0) {
            size_t free_mem = sys_get_free_memory();
            if (argc == 2 && strcmp(argv[1], "-h") == 0) {
                // Human-readable format
                if (free_mem >= 1024 * 1024) {
                    printf("Free memory: %lu MB\n", free_mem / (1024 * 1024));
                } else if (free_mem >= 1024) {
                    printf("Free memory: %lu KB\n", free_mem / 1024);
                } else {
                    printf("Free memory: %lu Bytes\n", free_mem);
                }
            } else {
                // Default: bytes
                printf("Free memory: %lu Bytes\n", free_mem);
            }
        } else if (strcmp(argv[0], "mkfs") == 0) {
            sys_mkfs();
        } else if (strcmp(argv[0], "statfs") == 0) {
            sys_statfs();
        } else if (strcmp(argv[0], "cd") == 0) {
            if (argc < 2) {
                printf("Error: cd needs a path\n");
            } else if (sys_cd(argv[1]) != 0) {
                printf("Error: cd %s failed\n", argv[1]);
            }
        } else if (strcmp(argv[0], "mkdir") == 0) {
            if (argc < 2) {
                printf("Error: mkdir needs a path\n");
            } else if (sys_mkdir(argv[1]) != 0) {
                printf("Error: mkdir %s failed\n", argv[1]);
            }
        } else if (strcmp(argv[0], "rmdir") == 0) {
            if (argc < 2) {
                printf("Error: rmdir needs a path\n");
            } else if (sys_rmdir(argv[1]) != 0) {
                printf("Error: rmdir %s failed\n", argv[1]);
            }
        } else if (strcmp(argv[0], "ls") == 0) {
            char *path = ".";
            int option = 0;
            if (argc >= 2 && strcmp(argv[1], "-l") == 0) {
                option = 1;
                if (argc >= 3) {
                    path = argv[2];
                }
            } else if (argc >= 2) {
                path = argv[1];
            }
            sys_ls(path, option);
        } else {
            printf("Error: Unknown command %s\n", buff);
        }

        printf("> root@UCAS_OS: ");
    }

    return 0;
}
