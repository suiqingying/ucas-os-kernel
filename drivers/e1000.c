#include <assert.h>
#include <e1000.h>
#include <os/net.h>
#include <os/string.h>
#include <os/time.h>
#include <pgtable.h>
#include <type.h>

// E1000 Registers Base Pointer
volatile uint8_t *e1000; // use virtual memory address

// E1000 Tx & Rx Descriptors
static struct e1000_tx_desc tx_desc_array[TXDESCS] __attribute__((aligned(16)));
static struct e1000_rx_desc rx_desc_array[RXDESCS] __attribute__((aligned(16)));

// E1000 Tx & Rx packet buffer
static char tx_pkt_buffer[TXDESCS][TX_PKT_SIZE];
static char rx_pkt_buffer[RXDESCS][RX_PKT_SIZE];

// Fixed Ethernet MAC Address of E1000
static const uint8_t enetaddr[6] = {0x00, 0x0a, 0x35, 0x00, 0x1e, 0x53};

static uint32_t tx_tail;
static uint32_t rx_tail;

/**
 * e1000_reset - Reset Tx and Rx Units; mask and clear all interrupts.
 **/
static void e1000_reset(void) {
    /* Turn off the ethernet interface */
    e1000_write_reg(e1000, E1000_RCTL, 0);
    e1000_write_reg(e1000, E1000_TCTL, 0);

    /* Clear the transmit ring */
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, 0);

    /* Clear the receive ring */
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, 0);

    /**
     * Delay to allow any outstanding PCI transactions to complete before
     * resetting the device
     */
    latency(1);

    /* Clear interrupt mask to stop board from generating interrupts */
    e1000_write_reg(e1000, E1000_IMC, 0xffffffff);

    /* Clear any pending interrupt events. */
    while (0 != e1000_read_reg(e1000, E1000_ICR));
}

/**
 * e1000_configure_tx - Configure 8254x Transmit Unit after Reset
 **/
static void e1000_configure_tx(void) {
    /* Initialize tx descriptors */
    for (int i = 0; i < TXDESCS; i++) {
        tx_desc_array[i].addr = kva2pa((uintptr_t)tx_pkt_buffer[i]);
        tx_desc_array[i].length = 0;
        tx_desc_array[i].cso = 0;
        tx_desc_array[i].cmd = 0;
        tx_desc_array[i].status = E1000_TXD_STAT_DD;
        tx_desc_array[i].css = 0;
        tx_desc_array[i].special = 0;
    }
    local_flush_dcache();

    /* Set up the Tx descriptor base address and length */
    uintptr_t tx_desc_pa = kva2pa((uintptr_t)tx_desc_array);
    e1000_write_reg(e1000, E1000_TDBAL, (uint32_t)tx_desc_pa);
    e1000_write_reg(e1000, E1000_TDBAH, (uint32_t)(tx_desc_pa >> 32));
    e1000_write_reg(e1000, E1000_TDLEN, (uint32_t)(TXDESCS * sizeof(struct e1000_tx_desc)));

    /* Set up the HW Tx Head and Tail descriptor pointers */
    tx_tail = 0;
    e1000_write_reg(e1000, E1000_TDH, 0);
    e1000_write_reg(e1000, E1000_TDT, tx_tail);

    /* Program the Transmit Control Register */
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP | (E1000_TCTL_CT & (0x10u << 4)) | (E1000_TCTL_COLD & (0x40u << 12));
    e1000_write_reg(e1000, E1000_TCTL, tctl); // ct设为0x10, cold设为0x40

    uint32_t tipg =
        (uint32_t)DEFAULT_82543_TIPG_IPGT_COPPER |
        ((uint32_t)DEFAULT_82543_TIPG_IPGR1 << E1000_TIPG_IPGR1_SHIFT) |
        ((uint32_t)DEFAULT_82543_TIPG_IPGR2 << E1000_TIPG_IPGR2_SHIFT);
    e1000_write_reg(e1000, E1000_TIPG, tipg);
}

/**
 * e1000_configure_rx - Configure 8254x Receive Unit after Reset
 **/
static void e1000_configure_rx(void) {
    /*  Set e1000 MAC Address to RAR[0] */
    uint32_t ral = (uint32_t)enetaddr[0] | ((uint32_t)enetaddr[1] << 8) |
                   ((uint32_t)enetaddr[2] << 16) |
                   ((uint32_t)enetaddr[3] << 24);
    uint32_t rah = (uint32_t)enetaddr[4] | ((uint32_t)enetaddr[5] << 8) |
                   E1000_RAH_AV;
    e1000_write_reg_array(e1000, E1000_RA, 0, ral);
    e1000_write_reg_array(e1000, E1000_RA, 1, rah);

    /* Initialize rx descriptors */
    for (int i = 0; i < RXDESCS; i++) {
        rx_desc_array[i].addr = kva2pa((uintptr_t)rx_pkt_buffer[i]);
        rx_desc_array[i].length = 0;
        rx_desc_array[i].csum = 0;
        rx_desc_array[i].status = 0;
        rx_desc_array[i].errors = 0;
        rx_desc_array[i].special = 0;
    }
    local_flush_dcache();

    /* Set up the Rx descriptor base address and length */
    uintptr_t rx_desc_pa = kva2pa((uintptr_t)rx_desc_array);
    e1000_write_reg(e1000, E1000_RDBAL, (uint32_t)rx_desc_pa);
    e1000_write_reg(e1000, E1000_RDBAH, (uint32_t)(rx_desc_pa >> 32));
    e1000_write_reg(e1000, E1000_RDLEN, (uint32_t)(RXDESCS * sizeof(struct e1000_rx_desc)));

    /* Set up the HW Rx Head and Tail descriptor pointers */
    rx_tail = RXDESCS - 1;
    e1000_write_reg(e1000, E1000_RDH, 0);
    e1000_write_reg(e1000, E1000_RDT, rx_tail);

    /* Program the Receive Control Register */
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_SZ_2048 |
                    E1000_RCTL_RDMTS_HALF;
    e1000_write_reg(e1000, E1000_RCTL, rctl);

    /* Enable RXDMT0 Interrupt */
    e1000_write_reg(e1000, E1000_IMS, E1000_IMS_RXDMT0);
}

/**
 * e1000_init - Initialize e1000 device and descriptors
 **/
void e1000_init(void) {
    /* Reset E1000 Tx & Rx Units; mask & clear all interrupts */
    e1000_reset();

    /* Configure E1000 Tx Unit */
    e1000_configure_tx();

    /* Configure E1000 Rx Unit */
    e1000_configure_rx();
}

/**
 * e1000_transmit - Transmit packet through e1000 net device
 * @param txpacket - The buffer address of packet to be transmitted
 * @param length - Length of this packet
 * @return - Number of bytes that are transmitted successfully
 **/
int e1000_transmit(void *txpacket, int length) {
    /* Transmit one packet from txpacket */
    if (length <= 0) {
        return 0;
    }

    uint32_t tail = tx_tail;
    struct e1000_tx_desc *desc = &tx_desc_array[tail];
    if ((desc->status & E1000_TXD_STAT_DD) == 0) {
        return -1;
    }

    int tx_len = length > TX_PKT_SIZE ? TX_PKT_SIZE : length;
    memcpy((uint8_t *)tx_pkt_buffer[tail], (const uint8_t *)txpacket, (uint32_t)tx_len);
    local_flush_dcache();

    desc->length = (uint16_t)tx_len;
    desc->cso = 0;
    desc->cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_RS | E1000_TXD_CMD_IFCS;
    desc->status = 0;
    local_flush_dcache();

    tx_tail = (tail + 1) % TXDESCS;
    e1000_write_reg(e1000, E1000_TDT, tx_tail);

    return tx_len;
}

/**
 * e1000_poll - Receive packet through e1000 net device
 * @param rxbuffer - The address of buffer to store received packet
 * @return - Length of received packet
 **/
int e1000_poll(void *rxbuffer) {
    /* Receive one packet and put it into rxbuffer */
    uint32_t next = (rx_tail + 1) % RXDESCS;
    struct e1000_rx_desc *desc = &rx_desc_array[next];
    if ((desc->status & E1000_RXD_STAT_DD) == 0) {
        return 0;
    }

    int rx_len = desc->length;
    if (rx_len > RX_PKT_SIZE) {
        rx_len = RX_PKT_SIZE;
    }
    memcpy((uint8_t *)rxbuffer, (const uint8_t *)rx_pkt_buffer[next],
           (uint32_t)rx_len);

    desc->status = 0;
    desc->length = 0;
    desc->errors = 0;
    local_flush_dcache();

    rx_tail = next;
    e1000_write_reg(e1000, E1000_RDT, rx_tail);

    return rx_len;
}

void e1000_handle_txqe(void) {
    /* Disable TXQE to avoid interrupt storm when queue stays empty. */
    e1000_write_reg(e1000, E1000_IMC, E1000_IMC_TXQE);
    net_unblock_send();
}

void e1000_handle_rxdmt0(void) {
    net_unblock_recv();
}

void e1000_handle_irq(void) {
    uint32_t icr = e1000_read_reg(e1000, E1000_ICR);
    if (icr & E1000_ICR_TXQE) {
        e1000_handle_txqe();
    }
    if (icr & E1000_ICR_RXDMT0) {
        e1000_handle_rxdmt0();
    }
}
