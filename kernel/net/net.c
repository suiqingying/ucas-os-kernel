#include <e1000.h>
#include <type.h>
#include <os/sched.h>
#include <os/string.h>
#include <os/list.h>
#include <os/smp.h>

static LIST_HEAD(send_block_queue);
static LIST_HEAD(recv_block_queue);

int do_net_send(void *txpacket, int length)
{
    // Transmit one network packet via e1000 device
    int sent;
    while ((sent = e1000_transmit(txpacket, length)) < 0) {
        do_scheduler();
    }

    // TODO: [p5-task3] Call do_block when e1000 transmit queue is full
    // TODO: [p5-task4] Enable TXQE interrupt if transmit queue is full

    return sent;  // Bytes it has transmitted
}

int do_net_recv(void *rxbuffer, int pkt_num, int *pkt_lens)
{
    // Receive one network packet via e1000 device
    if (pkt_num <= 0) {
        return 0;
    }

    int total = 0;
    for (int i = 0; i < pkt_num; i++) {
        int len;
        while ((len = e1000_poll((uint8_t *)rxbuffer + total)) <= 0) {
            do_scheduler();
        }
        if (pkt_lens) {
            pkt_lens[i] = len;
        }
        total += len;
    }
    // TODO: [p5-task3] Call do_block when there is no packet on the way

    return total;  // Bytes it has received
}

void net_handle_irq(void)
{
    // TODO: [p5-task4] Handle interrupts from network device
}