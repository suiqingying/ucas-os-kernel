#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#define CHUNK_SIZE 2048

static void fletcher_update(const uint8_t *data, int n, uint16_t *sum1, uint16_t *sum2) {
    for (int i = 0; i < n; i++) {
        *sum1 = (uint16_t)((*sum1 + data[i]) % 0xff);
        *sum2 = (uint16_t)((*sum2 + *sum1) % 0xff);
    }
}

int main(int argc, char *argv[]) {
    int expect_size = 0;
    uint32_t report_interval = 64 * 1024;
    uint8_t buf[CHUNK_SIZE];
    uint8_t size_buf[4];
    int size_got = 0;
    uint32_t total_size = 0;
    uint32_t data_target = 0;
    uint32_t received = 0;
    uint32_t last_report = 0;
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--file") == 0 ||
            strcmp(argv[i], "-s") == 0) {
            expect_size = 1;
        } else if (strcmp(argv[i], "-i") == 0 && i + 1 < argc) {
            report_interval = (uint32_t)atoi(argv[++i]);
        }
    }

    while (1) {
        int nbytes = CHUNK_SIZE;
        int ret = sys_net_recv_stream(buf, &nbytes);
        if (ret <= 0) {
            continue;
        }

        int offset = 0;
        if (expect_size && size_got < 4) {
            int need = 4 - size_got;
            int take = (nbytes < need) ? nbytes : need;
            memcpy(size_buf + size_got, buf, take);
            size_got += take;
            offset += take;
            if (size_got == 4) {
                total_size = (uint32_t)size_buf[0] |
                             ((uint32_t)size_buf[1] << 8) |
                             ((uint32_t)size_buf[2] << 16) |
                             ((uint32_t)size_buf[3] << 24);
                if (total_size >= 4) {
                    data_target = total_size - 4;
                } else {
                    data_target = 0;
                }
                printf("[RECV_STREAM] total=%u data=%u\n", total_size, data_target);
            }
        }

        if (!expect_size || size_got == 4) {
            if (offset < nbytes) {
                int data_len = nbytes - offset;
                if (expect_size) {
                    uint32_t remain = (data_target > received) ? (data_target - received) : 0;
                    if ((uint32_t)data_len > remain) {
                        data_len = (int)remain;
                    }
                }
                if (data_len > 0) {
                    fletcher_update(buf + offset, data_len, &sum1, &sum2);
                    received += (uint32_t)data_len;
                }
            }
        }

        if (expect_size) {
            if (received >= data_target) {
                break;
            }
        } else if (report_interval > 0 && received - last_report >= report_interval) {
            uint16_t checksum = (uint16_t)((sum2 << 8) | sum1);
            printf("[RECV_STREAM] received=%u checksum=0x%04x\n", received, checksum);
            last_report = received;
        }
    }

    if (expect_size) {
        uint16_t checksum = (uint16_t)((sum2 << 8) | sum1);
        printf("[RECV_STREAM] received=%u checksum=0x%04x\n", received, checksum);
    }
    return 0;
}
