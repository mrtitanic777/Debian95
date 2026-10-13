/* Debian95 — PS/2 keyboard driver (scancode set 1, US layout). IRQ1 pushes
 * translated characters into a ring buffer drained by kbd_poll(). */
#include <debian95/input.h>
#include <debian95/rtl.h>

/* make-code -> ASCII (unshifted). 0 = ignore. */
static const char g_normal[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=', 8, '\t',
    'q','w','e','r','t','y','u','i','o','p','[',']','\n', 0, 'a','s',
    'd','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x','c','v',
    'b','n','m',',','.','/', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
};
static const char g_shift[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+', 8, '\t',
    'Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0, 'A','S',
    'D','F','G','H','J','K','L',':','"','~', 0, '|','Z','X','C','V',
    'B','N','M','<','>','?', 0, '*', 0, ' ', 0,  0,  0,  0,  0,  0,
};

#define KBUF 128
static volatile char g_buf[KBUF];
static volatile u32  g_head, g_tail;
static int g_shift_down;
static int g_ctrl_down;
static int g_e0;

/* arrow key codes delivered to apps */
#define K_UP    0x91
#define K_DOWN  0x92
#define K_LEFT  0x93
#define K_RIGHT 0x94

static void push(char c) {
    u32 n = (g_head + 1) % KBUF;
    if (n != g_tail) { g_buf[g_head] = c; g_head = n; }
}

void kbd_init(void) {
    g_head = g_tail = 0;
    g_shift_down = 0;
    while (inb(0x64) & 1) (void)inb(0x60);   /* drain any pending byte */
}

void kbd_irq(void) {
    u8 sc = inb(0x60);
    if (sc == 0xE0) { g_e0 = 1; return; }              /* extended-key prefix */
    if (g_e0) {
        g_e0 = 0;
        if (sc == 0x48) push((char)K_UP);
        else if (sc == 0x50) push((char)K_DOWN);
        else if (sc == 0x4B) push((char)K_LEFT);
        else if (sc == 0x4D) push((char)K_RIGHT);
        return;
    }
    if (sc == 0x2A || sc == 0x36) { g_shift_down = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { g_shift_down = 0; return; }
    if (sc == 0x1D) { g_ctrl_down = 1; return; }
    if (sc == 0x9D) { g_ctrl_down = 0; return; }
    if (sc & 0x80) return;                    /* break code */
    char c = g_shift_down ? g_shift[sc] : g_normal[sc];
    if (!c) return;
    if (g_ctrl_down && c >= 'a' && c <= 'z') c = (char)(c - 'a' + 1);   /* Ctrl+letter -> 1..26 */
    push(c);
}

int kbd_poll(void) {
    if (g_tail == g_head) return 0;
    char c = g_buf[g_tail];
    g_tail = (g_tail + 1) % KBUF;
    return (int)(u8)c;
}
