/* Debian95 — interactive shell (a managed window) + the desktop event loop. */
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/input.h>
#include <debian95/ke.h>
#include <debian95/mm.h>
#include <debian95/nt.h>
#include <debian95/ps.h>
#include <debian95/net.h>
#include <debian95/comctl.h>
#include <debian95/rtl.h>

#define C_CON_BG  RGB(0x00, 0x00, 0x28)
#define C_CON_FG  RGB(0x60, 0xFF, 0x90)
#define C_CON_IN  RGB(0xFF, 0xFF, 0xFF)

#define COLS 108
#define ROWS 16

static char sh_out[ROWS][COLS];
static int  sh_nout;
static char sh_in[COLS];
static int  sh_inlen;

static void utoa(u32 v, char *b) {
    char tmp[12]; int i = 0;
    if (v == 0) { b[0] = '0'; b[1] = 0; return; }
    while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
    int j = 0; while (i) b[j++] = tmp[--i]; b[j] = 0;
}
static void append(char *dst, const char *src) {
    int d = 0; while (dst[d]) d++;
    int s = 0; while (src[s] && d < COLS - 1) dst[d++] = src[s++];
    dst[d] = 0;
}
static void sh_print(const char *s) {
    if (sh_nout >= ROWS) {
        for (int r = 1; r < ROWS; r++)
            for (int c = 0; c < COLS; c++) sh_out[r - 1][c] = sh_out[r][c];
        sh_nout = ROWS - 1;
    }
    int i = 0; while (s[i] && i < COLS - 1) { sh_out[sh_nout][i] = s[i]; i++; }
    sh_out[sh_nout][i] = 0; sh_nout++;
}

/* streamed output (from launched programs / cat) into the console */
static char sh_cur[COLS];
static int  sh_curlen;
static void sh_flush(void) { if (sh_curlen) { sh_cur[sh_curlen] = 0; sh_print(sh_cur); sh_curlen = 0; } }
static void sh_putc(char c) {
    if (c == '\n') { sh_flush(); return; }
    if (c < 32 || c > 126) return;
    if (sh_curlen >= COLS - 1) sh_flush();
    sh_cur[sh_curlen++] = c;
}
void shell_write(const char *s, u32 n) { for (u32 i = 0; i < n; i++) sh_putc(s[i]); }

/* uppercase a token into an 8.3-ish name buffer */
static void upcopy(const char *in, char *out) {
    int i = 0;
    while (in[i] && i < 15) { char c = in[i]; out[i] = (c >= 'a' && c <= 'z') ? c - 32 : c; i++; }
    out[i] = 0;
}

static void run_command(const char *cmd) {
    char echo[COLS]; echo[0] = '>'; echo[1] = ' '; echo[2] = 0; append(echo, cmd); sh_print(echo);
    if (strcmp(cmd, "help") == 0) {
        sh_print("commands: help ver mem ls cat run edit ps kill <pid> clear echo");
        sh_print("network:  net ping dhcp  ns <host>  wget <url>  dl <url> [name]  firewall [on|off]");
    } else if (strcmp(cmd, "ps") == 0) {
        TaskInfo ti[8];
        int n = task_list(ti, 8);
        sh_print("  PID  STATE  NAME");
        for (int i = 0; i < n; i++) {
            char l[COLS]; char num[12];
            l[0] = ' '; l[1] = ' '; l[2] = 0;
            utoa((u32)ti[i].pid, num); append(l, num);
            append(l, ti[i].state == 3 ? "  dead   " : "  run    ");
            append(l, ti[i].name);
            sh_print(l);
        }
    } else if (cmd[0] == 'k' && cmd[1] == 'i' && cmd[2] == 'l' && cmd[3] == 'l' && cmd[4] == ' ') {
        u32 pid = 0; for (const char *p = cmd + 5; *p >= '0' && *p <= '9'; p++) pid = pid * 10 + (*p - '0');
        char l[COLS] = "kill "; char num[12]; utoa(pid, num); append(l, num);
        append(l, task_kill(pid) == 0 ? ": terminated" : ": no such pid");
        sh_print(l);
    } else if (cmd[0] == 'c' && cmd[1] == 'a' && cmd[2] == 't' && cmd[3] == ' ') {
        char name[16]; upcopy(cmd + 4, name);
        u8 *buf; u32 sz;
        if (FsReadAlloc(name, &buf, &sz) == 0) {
            for (u32 i = 0; i < sz; i++) sh_putc((char)buf[i]);
            sh_flush(); kfree(buf);
        } else { char l[COLS] = "cat: not found: "; append(l, name); sh_print(l); }
    } else if (strcmp(cmd, "files") == 0) {
        files_open();
        sh_print("opened the file manager");
    } else if (strcmp(cmd, "paint") == 0) {
        paint_open();
        sh_print("opened paint");
    } else if (strcmp(cmd, "web") == 0) {
        browser_open();
        sh_print("opened the web browser");
    } else if (strcmp(cmd, "beep") == 0) {
        beep(880);
        sh_print("beep!");
    } else if (strcmp(cmd, "net") == 0) {
        char l[COLS]; net_status(l, sizeof(l)); sh_print(l);
    } else if (strcmp(cmd, "ping") == 0) {
        sh_print("pinging gateway 10.0.2.2 ...");
        int rc = net_ping(net_gateway_ip());
        sh_print(rc == 0 ? "  reply received." : "  no reply (timeout).");
    } else if (strcmp(cmd, "dhcp") == 0) {
        sh_print("requesting a lease via DHCP ...");
        if (net_dhcp() == 0) { char l[COLS] = "  got lease -> "; char s[COLS];
            net_status(s, sizeof(s)); append(l, s); sh_print(l); }
        else sh_print("  DHCP failed (no server?)");
    } else if (cmd[0] == 'n' && cmd[1] == 's' && cmd[2] == ' ') {
        u32 ip; char l[COLS] = "resolve "; append(l, cmd + 3);
        if (net_dns(cmd + 3, &ip) == 0) { char s[20]; net_ip_to_str(ip, s); append(l, " -> "); append(l, s); }
        else append(l, " -> failed");
        sh_print(l);
    } else if (cmd[0] == 'w' && cmd[1] == 'g' && cmd[2] == 'e' && cmd[3] == 't' && cmd[4] == ' ') {
        static char host[64]; static char resp[8192];
        const char *a = cmd + 5; int i = 0;
        while (a[i] && a[i] != '/' && i < 63) { host[i] = a[i]; i++; }
        host[i] = 0;
        const char *path = a[i] ? a + i : "/";
        char l[COLS] = "GET http://"; append(l, host); append(l, path); sh_print(l);
        int n = net_http_get(host, path, resp, sizeof(resp));
        if (n < 0) sh_print("  request failed");
        else {
            char info[COLS] = "  received "; char num[12]; utoa((u32)n, num);
            append(info, num); append(info, " bytes:"); sh_print(info);
            char line[COLS]; int c = 0, lines = 0;
            for (int j = 0; j < n && lines < 40; j++) {
                char ch = resp[j];
                if (ch == '\r') continue;
                if (ch == '\n') { line[c] = 0; sh_print(line); c = 0; lines++; }
                else if (c < COLS - 1) line[c++] = ch;
            }
            if (c) { line[c] = 0; sh_print(line); }
        }
    } else if (cmd[0] == 'd' && cmd[1] == 'l' && cmd[2] == ' ') {
        static char dbuf[262144];              /* 256 KiB download buffer */
        char url[128], name[16]; const char *a = cmd + 3;
        while (*a == ' ') a++;
        int u = 0; while (*a && *a != ' ' && u < 127) url[u++] = *a++; url[u] = 0;
        while (*a == ' ') a++;
        int nm = 0; while (*a && *a != ' ' && nm < 15) name[nm++] = *a++; name[nm] = 0;
        /* split url -> host[:port] + path */
        char host[96], path[96]; int h = 0; const char *p = url;
        while (*p && *p != '/' && h < 95) host[h++] = *p++; host[h] = 0;
        if (*p) { int q = 0; while (*p && q < 95) path[q++] = *p++; path[q] = 0; }
        else { path[0] = '/'; path[1] = 0; }
        /* derive save name from the last path segment if none given */
        if (!name[0]) {
            const char *base = path, *s = path;
            for (; *s; s++) if (*s == '/') base = s + 1;
            int q = 0; while (base[q] && q < 15) { char c = base[q]; name[q] = (c >= 'a' && c <= 'z') ? c - 32 : c; q++; }
            name[q] = 0;
            if (!name[0]) { const char *d = "INDEX.HTM"; int k = 0; while (d[k]) { name[k] = d[k]; k++; } name[k] = 0; }
        }
        char l[COLS] = "downloading "; append(l, url); append(l, " -> "); append(l, name); sh_print(l);
        int n = net_http_get(host, path, dbuf, sizeof(dbuf));
        if (n < 0) { sh_print("  download failed"); }
        else {
            int b = 0; for (; b + 3 < n; b++)
                if (dbuf[b]=='\r'&&dbuf[b+1]=='\n'&&dbuf[b+2]=='\r'&&dbuf[b+3]=='\n') { b += 4; break; }
            if (b + 3 >= n) b = 0;             /* no header found: keep all */
            int blen = n - b;
            int rc = FsWriteFile(name, (const u8 *)(dbuf + b), (u32)blen);
            char m[COLS]; char num[12];
            if (rc == 0) { m[0]=0; append(m, "  saved "); append(m, name); append(m, " (");
                utoa((u32)blen, num); append(m, num); append(m, " bytes)"); }
            else { m[0]=0; append(m, "  write failed (no volume?)"); }
            sh_print(m);
        }
    } else if (strcmp(cmd, "firewall") == 0) {
        char l[COLS] = "firewall: "; char num[12];
        append(l, net_firewall_on() ? "ON (default-deny)" : "OFF");
        append(l, "  dropped=");
        utoa(net_firewall_drops(), num); append(l, num);
        sh_print(l);
    } else if (strcmp(cmd, "firewall off") == 0) {
        net_firewall_set(0); sh_print("firewall disabled");
    } else if (strcmp(cmd, "firewall on") == 0) {
        net_firewall_set(1); sh_print("firewall enabled (default-deny)");
    } else if (cmd[0] == 'e' && cmd[1] == 'd' && cmd[2] == 'i' && cmd[3] == 't' && cmd[4] == ' ') {
        char name[16]; upcopy(cmd + 5, name);
        editor_open(name);
        char l[COLS] = "opened editor for "; append(l, name); sh_print(l);
    } else if (cmd[0] == 'w' && cmd[1] == 'i' && cmd[2] == 'n' && cmd[3] == ' ') {
        char name[16]; upcopy(cmd + 4, name);
        int pid = pe_run(name); sh_flush();
        if (pid < 0) { char l[COLS] = "win: not a PE / not found: "; append(l, name); sh_print(l); }
        else { char l[COLS] = "[PE launched at ring 3, pid "; char n[12]; utoa((u32)pid, n); append(l, n); append(l, "]"); sh_print(l); }
    } else if (cmd[0] == 'r' && cmd[1] == 'u' && cmd[2] == 'n' && cmd[3] == ' ') {
        char name[16]; upcopy(cmd + 4, name);
        int pid = ps_spawn(name);       /* launches concurrently; returns the pid */
        if (pid < 0) { char l[COLS] = "run: not found: "; append(l, name); sh_print(l); }
        else { char l[COLS] = "[launched pid "; char n[12]; utoa((u32)pid, n); append(l, n); append(l, ", running in background]"); sh_print(l); }
    } else if (cmd[0] == 'v' && cmd[1] == 'i' && cmd[2] == 'e' && cmd[3] == 'w' && cmd[4] == ' ') {
        char name[16]; upcopy(cmd + 5, name);
        viewer_open(name);
        char l[COLS] = "opened "; append(l, name); append(l, " in the image viewer"); sh_print(l);
    } else if (strcmp(cmd, "serve") == 0) {
        static const char *page =
            "<html><head><title>Debian95</title></head>"
            "<body bgcolor=\"#000080\" text=\"#ffffff\"><h1>Welcome to Debian95</h1>"
            "<p>This page is served by a from-scratch x86-64 operating system - its own "
            "BIOS bootloader, higher-half kernel, RTL8139 TCP/IP stack and HTTP server.</p>"
            "<p>Ring-3 processes, MS Sans Serif, and real Windows 95 shell32 icons.</p></body></html>";
        net_listen(80, page);
        sh_print("HTTP server listening on port 80 (guest 10.0.2.15).");
        sh_print("host: add QEMU -netdev user,hostfwd=tcp::8080-:80, then open http://localhost:8080");
    } else if (strcmp(cmd, "serve off") == 0) {
        net_unlisten();
        sh_print("HTTP server stopped.");
    } else if (strcmp(cmd, "find") == 0) {
        find_open();
        sh_print("opened Find (type a name to filter; click a result to open)");
    } else if (strcmp(cmd, "controls") == 0) {
        controls_open();
        sh_print("opened the common-controls dialog (ComCtl32-style)");
    } else if (strcmp(cmd, "rings") == 0) {
        char l[COLS]; cpu_ring_status(l, sizeof(l)); sh_print(l);
        sh_print("user programs run at CPL 3 in private address spaces; W^X enforced.");
    } else if (strcmp(cmd, "ver") == 0) {
        sh_print("Debian95 x86-64 - from-scratch NT-style OS");
    } else if (strcmp(cmd, "mem") == 0) {
        char line[COLS] = "free memory: "; char num[12];
        utoa((u32)((pmm_free_frames() * PAGE_SIZE) >> 20), num);
        append(line, num); append(line, " MiB"); sh_print(line);
    } else if (strcmp(cmd, "ls") == 0) {
        char names[16][16]; u32 sizes[16];
        int n = FsList(names, sizes, 16);
        if (n == 0) sh_print("(no volume)");
        for (int i = 0; i < n; i++) {
            char line[COLS]; char num[12];
            line[0] = 0; append(line, names[i]);
            int pad = 16 - (int)strlen(names[i]);
            for (int p = 0; p < pad; p++) append(line, " ");
            utoa(sizes[i], num); append(line, num); sh_print(line);
        }
    } else if (strcmp(cmd, "clear") == 0) {
        sh_nout = 0;
    } else if (cmd[0] == 'e' && cmd[1] == 'c' && cmd[2] == 'h' && cmd[3] == 'o' &&
               (cmd[4] == ' ' || cmd[4] == 0)) {
        sh_print(cmd[4] == ' ' ? cmd + 5 : "");
    } else if (cmd[0]) {
        char line[COLS] = "unknown command: "; append(line, cmd); sh_print(line);
    }
}

/* client-area painter for the shell window */
static void shell_paint(int cx, int cy, int cw, int ch) {
    fb_fill_rect(cx, cy, cw, ch, C_CON_BG);
    wm_bevel_sunken(cx, cy, cw, ch);
    font_select(1);                       /* fixed-width console font */
    int lh = font_height() + 2;
    int maxrows = (ch - 8) / lh; if (maxrows < 2) maxrows = 2;   /* clip to the client */
    int textrows = maxrows - 1;
    int start = sh_nout > textrows ? sh_nout - textrows : 0;     /* scroll to newest */
    int r = 0;
    for (int i = start; i < sh_nout; i++, r++)
        fb_text_t(cx + 6, cy + 4 + r * lh, sh_out[i], C_CON_FG);
    char prompt[COLS] = "> "; append(prompt, sh_in);
    int py = cy + 4 + r * lh;
    fb_text_t(cx + 6, py, prompt, C_CON_IN);
    fb_fill_rect(cx + 6 + (int)font_measure(prompt), py, font_height() / 2 + 2, font_height(), C_CON_IN);
    font_select(0);                       /* back to the UI font */
}

static void shell_input(char c) {
    if (c == '\n') { sh_in[sh_inlen] = 0; run_command(sh_in); sh_inlen = 0; sh_in[0] = 0; }
    else if (c == 8) { if (sh_inlen > 0) sh_in[--sh_inlen] = 0; }
    else if (c >= 32 && c < 127 && sh_inlen < COLS - 3) { sh_in[sh_inlen++] = c; sh_in[sh_inlen] = 0; }
}

/* desktop content */
static const char *w_welcome[] = {
    "A from-scratch NT-style OS,", "written in ASM, C and C++.", "",
    "Drag me by the title bar.", "Click X to close; the taskbar", "button reopens me.",
};
static const char *w_system[] = {
    "CPU : x86-64 (long mode)", "RAM : 255 MiB", "FS  : FAT32 (ATA)",
    "GUI : 1024x768x32", "WM  : draggable windows",
};

void desktop_run(void) {
    pic_remap();
    pit_init(100);
    kbd_init();
    mouse_init();
    fb_enable_backbuffer();
    task_init();               /* register the desktop as task 0 (scheduler root) */
    net_init();                /* bring up the NIC if present (firewall default-deny) */

    wm_add_text(60, 70, 380, 150, "Welcome", w_welcome, 6);
    wm_add_text(180, 300, 430, 150, "System", w_system, 5);
    Window *shellwin = wm_add_custom(60, 486, 904, 224, "Debian95 Shell", shell_paint, shell_input, 0);
    wm_set_text_cursor(shellwin);   /* I-beam over the console */
    files_open();      /* interactive file manager (replaces the static Files window) */
    wm_show(shellwin); /* focus the shell so typed commands work immediately */
    sh_print("Debian95 shell ready.  type 'help'.  try: edit README.TXT");
    sys_set_write_sink(shell_write);   /* launched programs print into the console */

    sti();
    wm_compose(); fb_present();

    u64 lastclock = 0;
    for (;;) {
        int c, dirty = 0;
        while ((c = kbd_poll()) != 0) { wm_dispatch_key((char)c); dirty = 1; }
        MouseEvent ev;
        while (mouse_poll(&ev)) { wm_mouse(ev.dx, ev.dy, ev.buttons); dirty = 1; }

        int act = wm_take_action();
        if (act == 1) {
            int pid = ps_spawn("INIT.ELF");
            char l[COLS] = "[Start: launched INIT.ELF pid "; char n[12];
            utoa((u32)pid, n); append(l, n); append(l, "]"); sh_print(l); dirty = 1;
        } else if (act == 2) {
            sh_print("Debian95: shutting down. it is now safe to close.");
            wm_compose(); fb_present();
            for (;;) __asm__ volatile ("cli; hlt");
        } else if (act == 3) { files_open(); dirty = 1; }
        else if (act == 4) { editor_open("README.TXT"); dirty = 1; }
        else if (act == 5) { paint_open(); dirty = 1; }
        else if (act == 6) { control_open(); dirty = 1; }
        else if (act == 7) { find_open(); dirty = 1; }
        else if (act == 8) { help_open(); dirty = 1; }
        else if (act == 9) { minesweeper_open(); dirty = 1; }
        else if (act == 10) { calc_open(); dirty = 1; }
        else if (act == 11) { clock_open(); dirty = 1; }
        else if (act == 12) { solitaire_open(); dirty = 1; }
        else if (act == 13) { mediaplayer_open("TESTVID.GIF"); dirty = 1; }
        else if (act == 20) { applet_open(2); dirty = 1; }
        else if (act == 30) { run_open(); dirty = 1; }

        net_poll();            /* service inbound frames (firewall-filtered) */
        if (mediaplayer_tick()) dirty = 1;     /* advance video playback */
        if (g_ticks - lastclock >= 100) { lastclock = g_ticks; task_reap(); dirty = 1; }  /* ~1 Hz: clock + reap */
        if (dirty) { wm_compose(); fb_present(); }
        __asm__ volatile ("hlt");
    }
}
