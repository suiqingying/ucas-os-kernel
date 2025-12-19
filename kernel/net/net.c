#include <e1000.h>
#include <os/list.h>
#include <os/net.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/time.h>
#include <type.h>

#define NET_ETH_HDR_LEN 14u
#define NET_IP_HDR_LEN 20u
#define NET_TCP_HDR_LEN 20u
#define NET_BASE_HDR_LEN (NET_ETH_HDR_LEN + NET_IP_HDR_LEN + NET_TCP_HDR_LEN)

#define RTP_MAGIC 0x45u
#define RTP_FLAG_DAT 0x01u
#define RTP_FLAG_RSD 0x02u
#define RTP_FLAG_ACK 0x04u
#define RTP_HDR_LEN 8u

#define STREAM_BUF_SIZE (64u * 1024u)
#define STREAM_OFO_MAX 64u
#define STREAM_MAX_PAYLOAD 1000u

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

typedef struct stream_ofo_entry {
    uint32_t seq;
    uint16_t len;
    uint8_t data[STREAM_MAX_PAYLOAD];
    int used;
} stream_ofo_entry_t;

typedef struct stream_state {
    uint32_t next_seq;
    uint32_t highest_seq;
    uint32_t ack_sent_seq;
    uint64_t last_progress_ticks;
    uint64_t last_rsd_ticks;
    uint32_t last_rsd_seq;
    int active;

    uint8_t peer_mac[ETH_ALEN];
    uint8_t local_mac[ETH_ALEN];
    uint32_t peer_ip;
    uint32_t local_ip;
    uint16_t peer_port;
    uint16_t local_port;
    int addr_valid;

    uint8_t buf[STREAM_BUF_SIZE];
    uint32_t buf_head;
    uint32_t buf_tail;
    uint32_t buf_count;

    stream_ofo_entry_t ofo[STREAM_OFO_MAX];
} stream_state_t;

static stream_state_t stream_state;
static uint8_t stream_rx_buf[RX_PKT_SIZE];
static uint64_t stream_rsd_timeout_ticks;
static uint64_t stream_rsd_retry_ticks;

static uint16_t net_get_be16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static uint32_t net_get_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void net_put_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xff);
}

static void net_put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)((v >> 16) & 0xff);
    p[2] = (uint8_t)((v >> 8) & 0xff);
    p[3] = (uint8_t)(v & 0xff);
}

static uint32_t stream_buf_space(void) {
    return STREAM_BUF_SIZE - stream_state.buf_count;
}

static int stream_buf_write(const uint8_t *data, uint32_t len) {
    if (len == 0) {
        return 1;
    }
    if (len > stream_buf_space()) {
        return 0;
    }
    uint32_t tail = stream_state.buf_tail;
    uint32_t first = STREAM_BUF_SIZE - tail;
    if (first > len) {
        first = len;
    }
    memcpy(&stream_state.buf[tail], data, first);
    if (len > first) {
        memcpy(stream_state.buf, data + first, len - first);
    }
    stream_state.buf_tail = (tail + len) % STREAM_BUF_SIZE;
    stream_state.buf_count += len;
    return 1;
}

static uint32_t stream_buf_read(uint8_t *out, uint32_t len) {
    if (len > stream_state.buf_count) {
        len = stream_state.buf_count;
    }
    if (len == 0) {
        return 0;
    }
    uint32_t head = stream_state.buf_head;
    uint32_t first = STREAM_BUF_SIZE - head;
    if (first > len) {
        first = len;
    }
    memcpy(out, &stream_state.buf[head], first);
    if (len > first) {
        memcpy(out + first, stream_state.buf, len - first);
    }
    stream_state.buf_head = (head + len) % STREAM_BUF_SIZE;
    stream_state.buf_count -= len;
    return len;
}

static int stream_ofo_find(uint32_t seq) {
    for (uint32_t i = 0; i < STREAM_OFO_MAX; i++) {
        if (stream_state.ofo[i].used && stream_state.ofo[i].seq == seq) {
            return (int)i;
        }
    }
    return -1;
}

static int stream_ofo_insert(uint32_t seq, const uint8_t *data, uint16_t len) {
    if (len == 0 || len > STREAM_MAX_PAYLOAD) {
        return 0;
    }
    if (stream_ofo_find(seq) >= 0) {
        return 0;
    }
    for (uint32_t i = 0; i < STREAM_OFO_MAX; i++) {
        if (!stream_state.ofo[i].used) {
            stream_state.ofo[i].used = 1;
            stream_state.ofo[i].seq = seq;
            stream_state.ofo[i].len = len;
            memcpy(stream_state.ofo[i].data, data, len);
            return 1;
        }
    }
    return 0;
}

static void stream_send_ctrl(uint8_t flag, uint32_t seq) {
    if (!stream_state.addr_valid) {
        return;
    }
    uint8_t pkt[NET_BASE_HDR_LEN + RTP_HDR_LEN];
    memset(pkt, 0, sizeof(pkt));

    struct ethhdr *eth = (struct ethhdr *)pkt;
    memcpy(eth->ether_dmac, stream_state.peer_mac, ETH_ALEN);
    memcpy(eth->ether_smac, stream_state.local_mac, ETH_ALEN);
    net_put_be16((uint8_t *)&eth->ether_type, ETH_P_IP);

    uint8_t *ip = pkt + NET_ETH_HDR_LEN;
    ip[0] = 0x45;
    ip[1] = 0;
    net_put_be16(&ip[2], NET_IP_HDR_LEN + NET_TCP_HDR_LEN + RTP_HDR_LEN);
    net_put_be16(&ip[4], 0);
    net_put_be16(&ip[6], 0);
    ip[8] = 64;
    ip[9] = 6;
    net_put_be16(&ip[10], 0);
    net_put_be32(&ip[12], stream_state.local_ip);
    net_put_be32(&ip[16], stream_state.peer_ip);

    uint8_t *tcp = ip + NET_IP_HDR_LEN;
    net_put_be16(&tcp[0], stream_state.local_port);
    net_put_be16(&tcp[2], stream_state.peer_port);
    net_put_be32(&tcp[4], 0);
    net_put_be32(&tcp[8], 0);
    tcp[12] = (NET_TCP_HDR_LEN / 4u) << 4;
    tcp[13] = 0x10;
    net_put_be16(&tcp[14], 65535);
    net_put_be16(&tcp[16], 0);
    net_put_be16(&tcp[18], 0);

    uint8_t *rtp = tcp + NET_TCP_HDR_LEN;
    rtp[0] = RTP_MAGIC;
    rtp[1] = flag;
    net_put_be16(&rtp[2], 0);
    net_put_be32(&rtp[4], seq);

    e1000_transmit(pkt, (int)sizeof(pkt));
}

static void stream_try_advance(void) {
    int progressed = 0;
    while (1) {
        int idx = stream_ofo_find(stream_state.next_seq);
        if (idx < 0) {
            break;
        }
        uint16_t len = stream_state.ofo[idx].len;
        if (len > stream_buf_space()) {
            break;
        }
        stream_buf_write(stream_state.ofo[idx].data, len);
        stream_state.ofo[idx].used = 0;
        stream_state.next_seq += len;
        progressed = 1;
    }
    if (progressed) {
        stream_state.last_progress_ticks = get_ticks();
        if (stream_state.ack_sent_seq != stream_state.next_seq) {
            stream_send_ctrl(RTP_FLAG_ACK, stream_state.next_seq);
            stream_state.ack_sent_seq = stream_state.next_seq;
        }
    }
}

static void stream_update_peer_info(const uint8_t *pkt, uint32_t ip_hdr_len) {
    memcpy(stream_state.peer_mac, pkt + 6, ETH_ALEN);
    memcpy(stream_state.local_mac, pkt, ETH_ALEN);

    const uint8_t *ip = pkt + NET_ETH_HDR_LEN;
    stream_state.peer_ip = net_get_be32(ip + 12);
    stream_state.local_ip = net_get_be32(ip + 16);

    const uint8_t *tcp = ip + ip_hdr_len;
    stream_state.peer_port = net_get_be16(tcp + 0);
    stream_state.local_port = net_get_be16(tcp + 2);
    stream_state.addr_valid = 1;
}

static void stream_handle_packet(const uint8_t *pkt, int pkt_len) {
    if (pkt_len <= (int)(NET_BASE_HDR_LEN + RTP_HDR_LEN)) {
        return;
    }
    uint32_t ip_hdr_len = (uint32_t)(pkt[NET_ETH_HDR_LEN] & 0x0f) * 4u;
    if (ip_hdr_len < NET_IP_HDR_LEN) {
        return;
    }
    uint32_t tcp_hdr_len =
        (uint32_t)((pkt[NET_ETH_HDR_LEN + ip_hdr_len + 12] >> 4) & 0x0f) * 4u;
    if (tcp_hdr_len < NET_TCP_HDR_LEN) {
        return;
    }
    uint32_t base_hdr_len = NET_ETH_HDR_LEN + ip_hdr_len + tcp_hdr_len;
    if (base_hdr_len + RTP_HDR_LEN > (uint32_t)pkt_len) {
        return;
    }

    const uint8_t *rtp = pkt + base_hdr_len;
    if (rtp[0] != RTP_MAGIC) {
        return;
    }
    uint8_t flags = rtp[1];
    if ((flags & RTP_FLAG_DAT) == 0) {
        return;
    }

    uint16_t len = net_get_be16(rtp + 2);
    uint32_t seq = net_get_be32(rtp + 4);
    if (len == 0 || len > STREAM_MAX_PAYLOAD) {
        return;
    }
    if (base_hdr_len + RTP_HDR_LEN + len > (uint32_t)pkt_len) {
        return;
    }

    if (!stream_state.addr_valid) {
        stream_update_peer_info(pkt, ip_hdr_len);
    }

    uint64_t now = get_ticks();
    if (!stream_state.active) {
        stream_state.active = 1;
        stream_state.last_progress_ticks = now;
    }

    uint32_t end_seq = seq + len;
    if (end_seq > stream_state.highest_seq) {
        stream_state.highest_seq = end_seq;
    }

    if (seq < stream_state.next_seq) {
        stream_send_ctrl(RTP_FLAG_ACK, stream_state.next_seq);
        stream_state.ack_sent_seq = stream_state.next_seq;
        return;
    }

    const uint8_t *payload = rtp + RTP_HDR_LEN;
    if (seq == stream_state.next_seq) {
        if (!stream_buf_write(payload, len)) {
            stream_ofo_insert(seq, payload, len);
            return;
        }
        stream_state.next_seq += len;
        stream_state.last_progress_ticks = now;
        if (stream_state.ack_sent_seq != stream_state.next_seq) {
            stream_send_ctrl(RTP_FLAG_ACK, stream_state.next_seq);
            stream_state.ack_sent_seq = stream_state.next_seq;
        }
        stream_try_advance();
        return;
    }

    stream_ofo_insert(seq, payload, len);
}

static void unblock_all(list_head *queue) {
    while (queue->next != queue) {
        do_unblock(queue->next);
    }
}

void net_unblock_send(void) {
    unblock_all(&send_block_queue);
}

void net_unblock_recv(void) {
    unblock_all(&recv_block_queue);
}

int do_net_send(void *txpacket, int length) {
    // Transmit one network packet via e1000 device
    int sent;
    while ((sent = e1000_transmit(txpacket, length)) < 0) {
        // Call do_block when e1000 transmit queue is full
        do_block(&current_running->list, &send_block_queue);
        // Enable TXQE interrupt if transmit queue is full
        e1000_write_reg(e1000, E1000_IMS, E1000_IMS_TXQE);
        do_scheduler();
    }
    return sent; // Bytes it has transmitted
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens) {
    // Receive one network packet via e1000 device
    if (pkt_num <= 0) {
        return 0;
    }

    int total = 0;
    for (int i = 0; i < pkt_num; i++) {
        int len;
        while ((len = e1000_poll((uint8_t *)rxbuffer + total)) <= 0) {
            // Call do_block when there is no packet on the way
            do_block(&current_running->list, &recv_block_queue);
            do_scheduler();
        }
        if (pkt_lens) {
            pkt_lens[i] = len;
        }
        total += len;
    }

    return total; // Bytes it has received
}

int do_net_recv_stream(void *buffer, int *nbytes) {
    if (!buffer || !nbytes) {
        return -1;
    }
    int limit = *nbytes;
    if (limit <= 0) {
        *nbytes = 0;
        return 0;
    }

    int copied = 0;
    while (copied == 0) {
        int rx_len;
        while ((rx_len = e1000_poll(stream_rx_buf)) > 0) {
            stream_handle_packet(stream_rx_buf, rx_len);
        }

        if (stream_state.buf_count > 0) {
            copied = (int)stream_buf_read((uint8_t *)buffer, (uint32_t)limit);
            stream_try_advance();
            break;
        }

        do_block(&current_running->list, &recv_block_queue);
        do_scheduler();
    }

    *nbytes = copied;
    return copied;
}

void net_stream_timer(void) {
    if (!stream_state.active) {
        return;
    }
    if (stream_state.highest_seq <= stream_state.next_seq) {
        return;
    }
    if (stream_rsd_timeout_ticks == 0) {
        uint64_t base = get_time_base();
        if (base == 0) {
            base = 1;
        }
        stream_rsd_timeout_ticks = base / 5;
        if (stream_rsd_timeout_ticks == 0) {
            stream_rsd_timeout_ticks = 1;
        }
        stream_rsd_retry_ticks = base / 10;
        if (stream_rsd_retry_ticks == 0) {
            stream_rsd_retry_ticks = stream_rsd_timeout_ticks;
        }
    }

    uint64_t now = get_ticks();
    if (now - stream_state.last_progress_ticks < stream_rsd_timeout_ticks) {
        return;
    }
    if (stream_state.last_rsd_seq == stream_state.next_seq &&
        now - stream_state.last_rsd_ticks < stream_rsd_retry_ticks) {
        return;
    }

    stream_send_ctrl(RTP_FLAG_RSD, stream_state.next_seq);
    stream_state.last_rsd_seq = stream_state.next_seq;
    stream_state.last_rsd_ticks = now;
}

void net_handle_irq(void) {
    // Handle interrupts from network device
    e1000_handle_irq();
}
