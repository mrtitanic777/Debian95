/* Debian95 — ComCtl32-style common controls (clean-room, classic 3D theme).
 * Immediate-mode draw helpers in the Windows-95 look: gray 3D buttons, check
 * boxes, radio buttons, group boxes, edit fields and progress bars. All colors
 * and coordinates are absolute framebuffer pixels. */
#ifndef DEBIAN95_COMCTL_H
#define DEBIAN95_COMCTL_H

#include <debian95/types.h>

void cc_button(int x, int y, int w, int h, const char *text, int pressed);
void cc_button_ex(int x, int y, int w, int h, const char *text, int pressed, int focused);
void cc_defbutton(int x, int y, int w, int h, const char *text, int pressed);
void cc_checkbox(int x, int y, const char *text, int checked);
void cc_radio(int x, int y, const char *text, int on);
void cc_groupbox(int x, int y, int w, int h, const char *text);
void cc_label(int x, int y, const char *text);
void cc_edit(int x, int y, int w, int h, const char *text, int focused);
void cc_progress(int x, int y, int w, int h, int pct);
void cc_scrollbar_v(int x, int y, int h, int thumb_top, int thumb_h);
void cc_tabs(int x, int y, int tabw, const char **labels, int n, int active);  /* fixed-width tabs */
int  cc_tab_hit(int x, int y, int tabw, int n, int mx, int my);                /* -> tab index or -1 */
int  cc_tabs_var(int x, int y, const char **labels, int n, int active);        /* variable-width tabs; returns total width */
int  cc_tab_hit_var(int x, int y, const char **labels, int n, int mx, int my); /* -> tab index or -1 */

/* open the common-controls demo dialog (a classic property-sheet look) */
void controls_open(void);

#endif /* DEBIAN95_COMCTL_H */
