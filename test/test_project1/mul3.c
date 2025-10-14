#include <kernel.h>
#include <batch.h>

char buf[20];
int main()
{
    int number;
    int *ptr = (int *)PIPE_LOC;
    number = *ptr;
    number *= 3;
    *ptr = number;
    my_itoa(number, 10, 0, 0, buf, 0);
    bios_putstr("[mul3] Result: ");
    bios_putstr(buf);
    bios_putchar('\n');
    return 0;
}