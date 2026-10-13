/* Debian95 — VGA text console (80x25 at physical 0xB8000). Addressed through the
 * memory direct map so it keeps working after the VMM installs kernel tables. */
#include <debian95/rtl.h>
#include <debian95/mm.h>

#define VGA_PHYS  0xB8000u
#define VGA_MEM   ((volatile u16 *)phys_to_virt(VGA_PHYS))
#define COLS      80
#define ROWS      25

static u8  g_color = 0x07;   /* light grey on black */
static int g_row = 0, g_col = 0;

void vga_set_color(u8 fg, u8 bg) {
    g_color = (u8)((bg << 4) | (fg & 0x0F));
}

static void clear(void) {
    u16 cell = (u16)(' ' | (g_color << 8));
    for (int i = 0; i < COLS * ROWS; i++)
        VGA_MEM[i] = cell;
    g_row = g_col = 0;
}

static void scroll(void) {
    for (int r = 1; r < ROWS; r++)
        for (int c = 0; c < COLS; c++)
            VGA_MEM[(r - 1) * COLS + c] = VGA_MEM[r * COLS + c];
    u16 blank = (u16)(' ' | (g_color << 8));
    for (int c = 0; c < COLS; c++)
        VGA_MEM[(ROWS - 1) * COLS + c] = blank;
    g_row = ROWS - 1;
}

void vga_init(void) {
    clear();
}

void vga_putc(char c) {
    if (c == '\n') { g_col = 0; g_row++; }
    else if (c == '\r') { g_col = 0; }
    else {
        VGA_MEM[g_row * COLS + g_col] = (u16)((u8)c | (g_color << 8));
        if (++g_col >= COLS) { g_col = 0; g_row++; }
    }
    if (g_row >= ROWS) scroll();
}
