/* Debian95 — Realtek RTL8139 NIC driver (polled). DMA buffers are static kernel
 * .bss (contiguous physical, addressed with kv2p). */
#include <debian95/net.h>
#include <debian95/mm.h>
#include <debian95/rtl.h>

/* register offsets from the I/O base */
#define R_MAC     0x00
#define R_TSD0    0x10
#define R_TSAD0   0x20
#define R_RBSTART 0x30
#define R_CMD     0x37
#define R_CAPR    0x38
#define R_IMR     0x3C
#define R_ISR     0x3E
#define R_RCR     0x44
#define R_CONFIG1 0x52

#define RX_SIZE   8192
#define RX_TOTAL  (RX_SIZE + 16 + 1500)

static u16 g_io;
static u8  g_mac[6];
static u8  g_rx[RX_TOTAL] __attribute__((aligned(16)));
static u8  g_tx[4][2048]  __attribute__((aligned(16)));
static int g_txcur;
static u16 g_rxptr;
static int g_ok;

int rtl8139_init(void) {
    PciDev dev;
    if (pci_find(0x10EC, 0x8139, &dev) != 0) return -1;
    g_io = dev.io_base;

    outb(g_io + R_CONFIG1, 0x00);           /* power on */
    outb(g_io + R_CMD, 0x10);               /* reset */
    for (int i = 0; i < 100000 && (inb(g_io + R_CMD) & 0x10); i++) { }

    outl(g_io + R_RBSTART, (u32)kv2p(g_rx));
    outl(g_io + R_RCR, 0xF | (1 << 7));     /* accept all + WRAP */
    outb(g_io + R_CMD, 0x0C);               /* RE | TE */
    outw(g_io + R_IMR, 0x0005);             /* (we still poll ISR) */

    for (int i = 0; i < 6; i++) g_mac[i] = inb(g_io + R_MAC + i);
    g_rxptr = 0;
    g_ok = 1;
    return 0;
}

int  rtl8139_present(void) { return g_ok; }
void rtl8139_mac(u8 *out)  { for (int i = 0; i < 6; i++) out[i] = g_mac[i]; }

int rtl8139_send(const void *data, u16 len) {
    if (!g_ok || len > 2048) return -1;
    int d = g_txcur;
    memcpy(g_tx[d], data, len);
    outl(g_io + R_TSAD0 + d * 4, (u32)kv2p(g_tx[d]));
    outl(g_io + R_TSD0 + d * 4, len);        /* clears OWN -> starts DMA */
    g_txcur = (g_txcur + 1) & 3;
    return 0;
}

/* Poll one received frame into out (returns length, 0 if none). */
int rtl8139_poll(void *out, u16 maxlen) {
    if (!g_ok) return 0;
    if (inb(g_io + R_CMD) & 0x01) return 0;  /* BUFE: buffer empty */

    u8 *p = g_rx + g_rxptr;
    u16 status = (u16)(p[0] | (p[1] << 8));
    u16 flen   = (u16)(p[2] | (p[3] << 8));   /* frame length incl 4-byte CRC */
    int n = 0;
    if ((status & 0x01) && flen >= 4 + 14 && flen <= RX_SIZE) {
        n = flen - 4;
        if (n > maxlen) n = maxlen;
        memcpy(out, p + 4, n);
    }
    g_rxptr = (u16)((g_rxptr + flen + 4 + 3) & ~3);
    g_rxptr %= RX_SIZE;
    outw(g_io + R_CAPR, (u16)(g_rxptr - 16));
    outw(g_io + R_ISR, 0x0001);               /* ack ROK */
    return n;
}
