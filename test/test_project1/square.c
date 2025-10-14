#include <kernel.h>
#include <batch.h>

char buf[20];
int main()
{
    int number;
    int *ptr = (int *)PIPE_LOC;
    number = *ptr;
    number = number * number;
    *ptr = number;
    my_itoa(number, 10, 0, 0, buf, 0);
    bios_putstr("[square] Result: ");
    bios_putstr(buf);
    bios_putchar('\n');
    return 0;
}