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
#include <os/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define SHELL_BEGIN 20
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

            for (int i = 1; i <= exec_argc; i++) {
                // printf("argv[%d]: %s\n", i, argv[i]);
                // strcpy(exec_argv[i - 1], argv[i]);
                exec_argv[i - 1] = argv[i];
                // printf("exec_argv[%d]: %s\n", i - 1, exec_argv[i - 1]);
            }
            // printf("exec_argc: %d\n", exec_argc);
            // printf("wait or nowait: %d\n", no_wait);
            pid_t pid = sys_exec(argv[1], exec_argc, exec_argv);
            if (pid == 0) {
                printf("Error: exec failed!\n");
            } else {
                printf("Info: excute %s successfully, pid = %d\n", argv[1], pid);
                if (!no_wait) sys_waitpid(pid);
            }
        } else if (strcmp("ls", argv[0]) == 0) {
            sys_list();
        } else if (strcmp("kill", argv[0]) == 0) {
            int pid = atoi(argv[1]);
            if (sys_kill(pid) == 0)
                printf("Info: Cannot find process with pid %d!\n", pid);
            else
                printf("Info: kill process %d successfully.\n", pid);
        } else if (strcmp("wait", argv[0]) == 0) {
            int pid = atoi(argv[1]);
            if (sys_waitpid(pid) == 0)
                printf("Info: Cannot find process with pid %d!\n", pid);
            else
                printf("Info: Excute waitpid successfully, pid = %d.\n", pid);
        } else {
            printf("Error: Unknown command %s\n", buff);
        }

        printf("> root@UCAS_OS: ");
        /************************************************************/
        /* Do not touch this comment. Reserved for future projects. */
        /************************************************************/
    }

    return 0;
}
