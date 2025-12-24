#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_COUNT 5000

static int int_to_str(int value, char *out)
{
    if (value <= 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }
    char tmp[16];
    int len = 0;
    while (value > 0 && len < (int)sizeof(tmp)) {
        tmp[len++] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (int i = 0; i < len; i++) {
        out[i] = tmp[len - 1 - i];
    }
    out[len] = '\0';
    return len;
}

static void make_path(int idx, char *out)
{
    const char *prefix = "/benchmeta/f";
    int pos = 0;
    for (int i = 0; prefix[i] != '\0'; i++) {
        out[pos++] = prefix[i];
    }
    pos += int_to_str(idx, out + pos);
    out[pos] = '\0';
}

static void create_files(int count)
{
    char path[32];
    for (int i = 0; i < count; i++) {
        make_path(i, path);
        int fd = sys_open(path, O_WRONLY);
        if (fd >= 0) {
            sys_close(fd);
        }
    }
}

static long open_pass(int count)
{
    char path[32];
    long start = sys_get_tick();
    for (int i = 0; i < count; i++) {
        make_path(i, path);
        int fd = sys_open(path, O_RDONLY);
        if (fd >= 0) {
            sys_close(fd);
        }
    }
    long end = sys_get_tick();
    return end - start;
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

int main(int argc, char **argv)
{
    int count = DEFAULT_COUNT;
    int mode_init = 0;
    int arg_idx = 1;

    if (argc >= 2) {
        if (strcmp(argv[1], "init") == 0) {
            mode_init = 1;
            arg_idx = 2;
        } else if (strcmp(argv[1], "run") == 0) {
            mode_init = 0;
            arg_idx = 2;
        }
    }
    if (argc > arg_idx) {
        int val = atoi(argv[arg_idx]);
        if (val > 0) {
            count = val;
        }
    }

    sys_mkdir("/benchmeta");
    if (mode_init) {
        printf("Create %d files under /benchmeta ...\n", count);
        create_files(count);
        printf("Init done. Reboot for cold cache test.\n");
        return 0;
    }

    int fd = sys_open("/benchmeta/f0", O_RDONLY);
    if (fd < 0) {
        printf("Error: /benchmeta not initialized. Run: exec meta_bench init %d\n", count);
        return 0;
    }
    sys_close(fd);

    long t1 = open_pass(count);
    long t2 = open_pass(count);
    print_ticks("lookup-1 (cold)", t1);
    print_ticks("lookup-2 (warm)", t2);
    return 0;
}
