#include <kernel.h>
#include <batch.h>

char buf[10];
int main()
{
    int number;
    int *ptr = (int *)PIPE_LOC;
    number = *ptr;
    number += 10;
    *ptr = number;
    my_itoa(number, 10, 0, 0, buf, 0);
    bios_putstr("[add10] Result: ");
    bios_putstr(buf);
    bios_putchar('\n');
    return 0;
}