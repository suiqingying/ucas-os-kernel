#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_MB 32
#define CHUNK_SIZE 4096

static char buf[CHUNK_SIZE];

static void fill_buf(void)
{
    for (int i = 0; i < CHUNK_SIZE; i++) {
        buf[i] = (char)(i & 0xff);
    }
}

static void print_ticks(const char *label, long ticks)
{
    long base = sys_get_timebase();
    if (base > 0) {
        long ms = (ticks * 1000) / base;
        printf("%s: %ld ticks (~%ld ms)\n", label, ticks, ms);
    } else {
        printf("%s: %ld ticks\n", label, ticks);
    }
}

static int write_file(const char *path, int mb)
{
    int fd = sys_open((char *)path, O_WRONLY);
    if (fd < 0) {
        printf("Error: open %s failed\n", path);
        return -1;
    }
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
    return (written >= total) ? 0 : -1;
}

static long read_file_once(const char *path, long *bytes_out)
{
    int fd = sys_open((char *)path, O_RDONLY);
    if (fd < 0) {
        printf("Error: open %s failed\n", path);
        return -1;
    }
    long bytes = 0;
    long start = sys_get_tick();
    int n = 0;
    while ((n = sys_read(fd, buf, CHUNK_SIZE)) > 0) {
        bytes += n;
    }
    long end = sys_get_tick();
    sys_close(fd);
    if (bytes_out) {
        *bytes_out = bytes;
    }
    return end - start;
}

int main(int argc, char **argv)
{
    const char *path = "/bench/cache.bin";
    sys_mkdir("/bench");
    fill_buf();

    if (argc >= 2 && strcmp(argv[1], "init") == 0) {
        int mb = DEFAULT_MB;
        if (argc >= 3) {
            int val = atoi(argv[2]);
            if (val > 0) {
                mb = val;
            }
        }
        printf("Init %dMB file at %s\n", mb, path);
        if (write_file(path, mb) == 0) {
            printf("Init done. Reboot for cold cache test.\n");
        }
        return 0;
    }

    long bytes = 0;
    long t1 = read_file_once(path, &bytes);
    if (t1 < 0) {
        return 0;
    }
    long t2 = read_file_once(path, NULL);
    printf("Read %ld bytes\n", bytes);
    print_ticks("read-1 (cold)", t1);
    print_ticks("read-2 (warm)", t2);
    return 0;
}
