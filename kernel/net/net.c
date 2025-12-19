#include <e1000.h>
#include <os/list.h>
#include <os/sched.h>
#include <os/smp.h>
#include <os/string.h>
#include <type.h>

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

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

void net_handle_irq(void) {
    // Handle interrupts from network device
    e1000_handle_irq();
}