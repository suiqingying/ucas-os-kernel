#include <stdio.h>
#include <unistd.h>
// #include <kernel.h>

int main(void)
{
    int print_location = 1;

    for (int i = 0;; i++)
    {
        sys_move_cursor(0, print_location);
        printf("> [TASK] This task is to test scheduler. (%d)", i);
        // sys_yield();
    }
}
