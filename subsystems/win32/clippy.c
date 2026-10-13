/* Debian95 — Clippy, the assistant reborn: a real local AI. The classic paperclip
 * character with googly eyes, backed by a genuine LLM (llama.cpp + a bundled
 * instruct model via sys_ai_*). Type a question, Clippy actually answers. The
 * generation runs async; clippy_tick() (called from the platform loop) polls for
 * the reply. Declarative dialog + an owner-drawn character. */
#include <debian95/win32.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

int sys_ai_available(void);
int sys_ai_ask(const char *msg);
int sys_ai_reply(char *out, int n);
void wm_post_action(int a);                 /* queue a desktop action for the platform */
void rtc_time(int *hh, int *mm);
void sys_date(int *y, int *mo, int *d, int *dow, int *hh, int *mm, int *ss);

enum { IDC_CLIP = 100, IDC_LOG, IDC_IN, IDC_ASK };

static const DLGITEM cl_items[] = {
    { WC_OWNERDRAW, 0,              8,   8,  46, 80, IDC_CLIP, 0 },
    { WC_LISTBOX,   0,             60,   8, 214, 98, IDC_LOG,  0 },
    { WC_EDIT,      0,             60, 112, 150, 14, IDC_IN,   "" },
    { WC_BUTTON,    BS_DEFPUSHBUTTON,214,112, 60, 14, IDC_ASK, "Ask" },
};
static const DLGTEMPLATE_L cl_tmpl = { "Clippy - Debian95 Assistant", 282, 134, cl_items, 4, DS_DLU };

static HWND g_dlg;
static int  g_thinking;

/* add text to the conversation log, word-wrapped, prefixed by who. */
static void say(HWND h, const char *who, const char *txt) {
    char line[64]; int k = 0;
    for (const char *p = who; *p; p++) line[k++] = *p;
    line[k++] = ':'; line[k++] = ' ';
    for (const char *p = txt; *p; p++) {
        if (*p == '\n') { line[k] = 0; DlgListAdd(h, IDC_LOG, line); k = 0; continue; }
        line[k++] = *p;
        if (k >= 42) {                      /* wrap: break at the last space if any */
            int br = k; while (br > 20 && line[br-1] != ' ') br--;
            if (br <= 20) br = k;
            char rest[64]; int r = 0; for (int i = br; i < k; i++) rest[r++] = line[i];
            line[br] = 0; DlgListAdd(h, IDC_LOG, line);
            k = 0; for (int i = 0; i < r; i++) line[k++] = rest[i];
        }
    }
    if (k) { line[k] = 0; DlgListAdd(h, IDC_LOG, line); }
    int n = DlgListCount(h, IDC_LOG); if (n > 0) DlgListSetSel(h, IDC_LOG, n - 1);   /* scroll to end */
}

/* thick rectangle outline (the paperclip wire) */
static void wire(int x, int y, int w, int h, int t, u32 c) {
    fb_fill_rect(x, y, w, t, c); fb_fill_rect(x, y + h - t, w, t, c);
    fb_fill_rect(x, y, t, h, c); fb_fill_rect(x + w - t, y, t, h, c);
}
static void clip_draw(int ox, int oy) {
    u32 sv = RGB(0xB8,0xC2,0xD0), hi = RGB(0xEC,0xF0,0xF6), dk = RGB(0x58,0x60,0x6C), blk = RGB(0,0,0), wht = RGB(0xFF,0xFF,0xFF);
    int cx = ox + 18;               /* clip centred in the owner-draw box */
    /* body: two nested bent-wire loops - the classic paperclip */
    wire(cx - 11, oy + 20, 24, 58, 3, sv);        /* outer loop  */
    wire(cx - 6,  oy + 14, 14, 50, 3, sv);        /* middle loop */
    wire(cx - 1,  oy + 20, 7,  30, 3, hi);        /* inner tail  */
    fb_fill_rect(cx - 11, oy + 76, 24, 2, dk);    /* soft base shadow */
    /* the expressive eyes + brows that make it Clippy, riding the top curve */
    int ex = cx - 10, ey = oy + 6;
    fb_fill_rect(ex, ey, 10, 10, wht);  fb_fill_rect(ex + 12, ey, 10, 10, wht);   /* whites */
    fb_rect(ex, ey, 10, 10, dk);         fb_rect(ex + 12, ey, 10, 10, dk);
    fb_fill_rect(ex + 4, ey + 4, 4, 4, blk); fb_fill_rect(ex + 16, ey + 4, 4, 4, blk);  /* pupils */
    fb_fill_rect(ex - 1, ey - 3, 10, 2, dk); fb_fill_rect(ex + 12, ey - 3, 10, 2, dk);  /* eyebrows */
}

/* say() with a formatted message */
static void sayf(HWND h, const char *who, const char *fmt, ...) {
    char buf[220]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    say(h, who, buf);
}

/* Clippy doesn't only chat - it DOES things. Map a natural request to a real
 * desktop action (launch an app, switch theme, lock, tell the time/date) and run
 * it via wm_post_action. Returns 1 if handled, 0 to fall through to the LLM. */
static const struct { const char *kw; int id; const char *name; } CL_APPS[] = {
    {"notepad",4,"Notepad"}, {"paint",5,"Paint"}, {"calculator",10,"Calculator"}, {"calc",10,"Calculator"},
    {"file manager",3,"the File Manager"}, {"explorer",3,"Windows Explorer"}, {"my computer",18,"My Computer"},
    {"terminal",23,"the Terminal"}, {"command prompt",15,"the MS-DOS Prompt"}, {"ms-dos",15,"the MS-DOS Prompt"},
    {"task manager",31,"Task Manager"}, {"control panel",6,"Control Panel"}, {"media player",13,"Media Player"},
    {"clock",11,"the Clock"}, {"calendar",61,"the Calendar"}, {"minesweeper",9,"Minesweeper"},
    {"solitaire",12,"Solitaire"}, {"freecell",24,"FreeCell"}, {"web browser",28,"the Web Browser"},
    {"browser",28,"the Web Browser"}, {"internet",28,"the Web Browser"}, {"registry",29,"the Registry Editor"},
    {"volume",25,"Volume Control"}, {"sound recorder",26,"Sound Recorder"}, {"wordpad",27,"WordPad"},
    {"vlc",42,"VLC"}, {"chromium",43,"Chromium"}, {"chrome",43,"Chromium"}, {"device manager",64,"Device Manager"},
    {"event viewer",65,"Event Viewer"}, {"network",66,"Network Neighborhood"}, {"firewall",74,"Windows Firewall"},
    {"remote access",73,"Remote Access"}, {"system restore",75,"System Restore"}, {"narrator",70,"Narrator"},
    {"snipping",69,"the Snipping Tool"}, {"character map",14,"Character Map"},
};
static int try_command(HWND h, const char *msg) {
    char m[256]; int i; for (i = 0; msg[i] && i < 255; i++) { char c = msg[i]; m[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; } m[i] = 0;
    /* theme / look */
    if (strstr(m, "theme") || strstr(m, "look like") || strstr(m, "skin")) {
        int t = -1; const char *nm = 0;
        if (strstr(m, "olive")) { t = 4; nm = "Windows XP Olive"; }
        else if (strstr(m, "silver")) { t = 5; nm = "Windows XP Silver"; }
        else if (strstr(m, "vista")) { t = 6; nm = "Windows Vista"; }
        else if (strstr(m, "7") || strstr(m, "seven") || strstr(m, "aero")) { t = 7; nm = "Windows 7"; }
        else if (strstr(m, "desert")) { t = 8; nm = "Desert"; }
        else if (strstr(m, "rose")) { t = 9; nm = "Rose"; }
        else if (strstr(m, "eggplant") || strstr(m, "purple")) { t = 10; nm = "Eggplant"; }
        else if (strstr(m, "spruce") || strstr(m, "green")) { t = 11; nm = "Spruce"; }
        else if (strstr(m, "brick") || strstr(m, "red")) { t = 12; nm = "Brick"; }
        else if (strstr(m, "high contrast") || strstr(m, "contrast")) { t = 13; nm = "High Contrast Black"; }
        else if (strstr(m, "xp")) { t = 2; nm = "Windows XP"; }
        else if (strstr(m, "98")) { t = 1; nm = "Windows 98"; }
        else if (strstr(m, "flat") || strstr(m, "modern")) { t = 3; nm = "Flat"; }
        else if (strstr(m, "95") || strstr(m, "classic")) { t = 0; nm = "classic Windows 95"; }
        if (t >= 0) { wm_post_action(90 + t); sayf(h, "Clippy", "You got it - switching to the %s theme!", nm); return 1; }
    }
    if (strstr(m, "lock")) { wm_post_action(51); say(h, "Clippy", "Locking the computer. See you soon!"); return 1; }
    if (strstr(m, "what time") || strstr(m, "the time")) {
        int hh = 12, mm = 0; rtc_time(&hh, &mm);
        sayf(h, "Clippy", "It's %d:%02d %s right now.", (hh % 12) ? (hh % 12) : 12, mm, hh < 12 ? "AM" : "PM"); return 1;
    }
    if (strstr(m, "what day") || strstr(m, "the date") || strstr(m, "todays date") || strstr(m, "today's date")) {
        int y = 0, mo = 0, d = 0, dow = 0, hh = 0, mm = 0, ss = 0; sys_date(&y, &mo, &d, &dow, &hh, &mm, &ss);
        static const char *const DOW[] = { "Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday" };
        sayf(h, "Clippy", "Today is %s, %d-%02d-%02d.", (dow >= 0 && dow < 7) ? DOW[dow] : "", y, mo, d); return 1;
    }
    if (strstr(m, "open") || strstr(m, "launch") || strstr(m, "start ") || strstr(m, "run ") || strstr(m, "bring up") || strstr(m, "go to")) {
        for (unsigned j = 0; j < sizeof CL_APPS / sizeof CL_APPS[0]; j++)
            if (strstr(m, CL_APPS[j].kw)) { wm_post_action(CL_APPS[j].id); sayf(h, "Clippy", "Opening %s for you!", CL_APPS[j].name); return 1; }
    }
    return 0;
}

static void ask_now(HWND h) {
    char m[256]; GetDlgItemTextW32(h, IDC_IN, m, sizeof m);
    if (!m[0] || g_thinking) return;
    say(h, "You", m);
    SetDlgItemTextW32(h, IDC_IN, "");
    if (try_command(h, m)) return;                      /* Clippy acted on it directly */
    if (sys_ai_available()) { sys_ai_ask(m); g_thinking = 1; say(h, "Clippy", "...let me think..."); }
    else say(h, "Clippy", "My brain isn't installed, but I can still open apps and change settings - try 'open Paint' or 'switch to the XP theme'.");
}

static LRESULT CALLBACK CL_DlgProc(HWND h, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_INITDIALOG:
            say(h, "Clippy", sys_ai_available() ? "Hi! I'm Clippy. Ask me anything - or tell me to open an app or change your theme!"
                                                : "Hi! I'm Clippy. Tell me to open an app (e.g. 'open Paint') or change your theme!");
            return 0;
        case WM_DRAWITEM:
            if ((int)wParam == IDC_CLIP) { RECT *r = (RECT *)lParam; fb_fill_rect(r->left, r->top, r->right-r->left, r->bottom-r->top, RGB(0xC0,0xC0,0xC0)); clip_draw(r->left, r->top); }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_ASK) ask_now(h);
            return 0;
        case WM_DESTROY: g_dlg = 0; g_thinking = 0; return 0;
        default: return DefWindowProc(h, msg, wParam, lParam);
    }
}

int clippy_tick(void) {       /* platform loop: poll the async reply */
    if (!g_dlg || !g_thinking) return 0;
    char r[1200];
    if (sys_ai_reply(r, sizeof r)) { g_thinking = 0; say(g_dlg, "Clippy", r[0] ? r : "(no answer)"); return 1; }
    return 0;
}
void clippy_open(void) { if (g_dlg) return; g_dlg = CreateDialogL(&cl_tmpl, CL_DlgProc, 0); }

/* test hook: open Clippy and ask a question programmatically (DEBIAN95_CLIPPY=...) */
void clippy_ask_test(const char *q) {
    clippy_open();
    if (!g_dlg || !q || !*q) return;
    say(g_dlg, "You", q);
    if (try_command(g_dlg, q)) return;                  /* exercise the do-things path */
    if (sys_ai_available()) { sys_ai_ask(q); g_thinking = 1; say(g_dlg, "Clippy", "...let me think..."); }
    else say(g_dlg, "Clippy", "My brain isn't installed, but I can open apps and change settings.");
}
