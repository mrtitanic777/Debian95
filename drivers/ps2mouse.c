/* Debian95 — PS/2 mouse driver (IRQ12). Decodes 3-byte packets into relative
 * movement + button events, queued for mouse_poll(). */
#include <debian95/input.h>
#include <debian95/rtl.h>

#define PS2_DATA 0x60
#define PS2_CMD  0x64

static void wait_in(void)  { for (u32 i = 0; i < 200000; i++) if (!(inb(PS2_CMD) & 2)) return; }
static void wait_out(void) { for (u32 i = 0; i < 200000; i++) if (inb(PS2_CMD) & 1) return; }

static void mouse_write(u8 v) {
    wait_in(); outb(PS2_CMD, 0xD4);
    wait_in(); outb(PS2_DATA, v);
    wait_out(); (void)inb(PS2_DATA);       /* ACK */
}

#define MQ 64
static volatile MouseEvent g_q[MQ];
static volatile u32 g_head, g_tail;
static u8  g_packet[3];
static int g_cnt;

void mouse_init(void) {
    g_head = g_tail = 0;
    g_cnt = 0;

    wait_in(); outb(PS2_CMD, 0xA8);        /* enable auxiliary device */

    wait_in(); outb(PS2_CMD, 0x20);        /* read controller config byte */
    wait_out(); u8 cfg = inb(PS2_DATA);
    cfg |= 0x02;                            /* enable IRQ12 */
    cfg &= ~0x20;                           /* enable mouse clock */
    wait_in(); outb(PS2_CMD, 0x60);
    wait_in(); outb(PS2_DATA, cfg);

    mouse_write(0xF6);                      /* set defaults */
    mouse_write(0xF4);                      /* enable data reporting */
}

void mouse_irq(void) {
    u8 b = inb(PS2_DATA);
    switch (g_cnt) {
        case 0:
            if (!(b & 0x08)) return;        /* resync: bit3 always set in byte 0 */
            g_packet[0] = b; g_cnt = 1; break;
        case 1:
            g_packet[1] = b; g_cnt = 2; break;
        case 2: {
            g_packet[2] = b; g_cnt = 0;
            int dx = g_packet[1];
            int dy = g_packet[2];
            if (g_packet[0] & 0x10) dx -= 256;
            if (g_packet[0] & 0x20) dy -= 256;
            u32 n = (g_head + 1) % MQ;
            if (n != g_tail) {
                g_q[g_head].dx = dx;
                g_q[g_head].dy = dy;
                g_q[g_head].buttons = g_packet[0] & 0x07;
                g_head = n;
            }
            break;
        }
    }
}

int mouse_poll(MouseEvent *ev) {
    if (g_tail == g_head) return 0;
    *ev = g_q[g_tail];
    g_tail = (g_tail + 1) % MQ;
    return 1;
}
