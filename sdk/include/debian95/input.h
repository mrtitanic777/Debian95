/* Debian95 — PS/2 input (keyboard + mouse). */
#ifndef DEBIAN95_INPUT_H
#define DEBIAN95_INPUT_H

#include <debian95/types.h>

/* keyboard */
void kbd_init(void);
void kbd_irq(void);      /* called from the IRQ1 handler */
int  kbd_poll(void);     /* next char (ASCII; '\n'=enter, 8=backspace), 0 if none */

/* mouse */
typedef struct { int dx, dy; u8 buttons; } MouseEvent;
void mouse_init(void);
void mouse_irq(void);    /* called from the IRQ12 handler */
int  mouse_poll(MouseEvent *ev);   /* 1 if an event was dequeued */

#endif /* DEBIAN95_INPUT_H */
