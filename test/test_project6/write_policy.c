#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_MB 8
#define CHUNK_SIZE 4096

static char buf[CHUNK_SIZE];

static void fill_buf(void)
{
    for (int i = 0; i < CHUNK_SIZE; i++) {
        buf[i] = (char)('A' + (i % 26));
    }
}

int main(int argc, char **argv)
{
    int mb = DEFAULT_MB;
    if (argc >= 2) {
        int val = atoi(argv[1]);
        if (val > 0) {
            mb = val;
        }
    }

    sys_mkdir("/benchpolicy");
    int fd = sys_open("/benchpolicy/policy.txt", O_WRONLY);
    if (fd < 0) {
        printf("Error: open policy.txt failed\n");
        return 0;
    }

    fill_buf();
    long total = (long)mb * 1024 * 1024;
    long written = 0;
    while (written < total) {
        int n = sys_write(fd, buf, CHUNK_SIZE);
        if (n <= 0) {
            break;
        }
        written += n;
    }
    sys_close(fd);

    printf("Wrote %dMB to /benchpolicy/policy.txt\n", mb);
    printf("Now power off to test write policy persistence.\n");
    return 0;
}
