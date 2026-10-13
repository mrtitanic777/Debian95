/* Debian95 — COMDLG32-lite: one shared Open/Save file dialog every app reuses,
 * so file picking looks and behaves identically everywhere (exactly like the
 * real OS). Callback-based to fit the non-blocking WM loop. See win32.h. */
#include <debian95/win32.h>
#include <debian95/wm.h>
#include <debian95/fb.h>
#include <debian95/font.h>
#include <debian95/comctl.h>
#include <debian95/nt.h>

static HWND g_dlg;            /* the shared Open/Save dialog (Win32 veneer) */
static char    g_title[48], g_filter[16], g_name[64];
static int     g_isSave, g_sel, g_scroll, g_editlen;
static void  (*g_onok)(const char *path);
static FsEntry g_all[192]; static int g_nall;
static FsEntry g_ent[192];  static int g_nent;   /* filtered view */

#define FACE  RGB(0xC0,0xC0,0xC0)
#define DK    RGB(0x00,0x00,0x00)

static int ieq(char a, char b) { if (a>='a'&&a<='z') a-=32; if (b>='a'&&b<='z') b-=32; return a==b; }
static int match_filter(const char *name) {
    if (!g_filter[0]) return 1;
    const char *f = g_filter; if (f[0]=='*'&&f[1]=='.') f += 1;    /* "*.TXT" -> ".TXT" */
    if (f[0]!='.') return 1;                                       /* unknown filter = all */
    /* find name's extension */
    int nl = 0; while (name[nl]) nl++;
    int dot = -1; for (int i=0;i<nl;i++) if (name[i]=='.') dot=i;
    if (dot<0) return 0;
    const char *ne = name+dot; int i=0; for (; f[i]&&ne[i]; i++) if (!ieq(f[i],ne[i])) return 0;
    return f[i]==0 && ne[i]==0;
}
static void refresh(void) {
    g_nall = FsListDir("", g_all, 192);
    g_nent = 0;
    for (int i=0;i<g_nall && g_nent<192;i++)
        if (!g_all[i].is_dir && match_filter(g_all[i].name)) g_ent[g_nent++] = g_all[i];
}

/* layout (client-relative) */
static void rects(int cw, int ch, int *lx,int *ly,int *lw,int *lh,int *rowh,int *vis) {
    *lx=12; *ly=10; *lw=cw-24; *lh=ch-92; *rowh=(int)font_height()+3;
    *vis = *lh / *rowh;
}
static void ed_rect(int cw,int ch,int *x,int *y,int *w,int *h){ *x=76; *y=ch-74; *w=cw-180; *h=22; }
static void btn_rects(int cw,int ch,int *bx,int *oy,int *cy2,int *bw,int *bh){ *bw=80;*bh=23;*bx=cw-92;*oy=ch-74;*cy2=ch-46; }

static void dlg_paint(int cx,int cy,int cw,int ch){
    fb_fill_rect(cx,cy,cw,ch,FACE);
    int lx,ly,lw,lh,rowh,vis; rects(cw,ch,&lx,&ly,&lw,&lh,&rowh,&vis);
    /* list box */
    fb_fill_rect(cx+lx,cy+ly,lw,lh,RGB(0xFF,0xFF,0xFF)); wm_bevel_sunken(cx+lx,cy+ly,lw,lh);
    fb_set_clip(cx+lx+2,cy+ly+2,lw-20,lh-4);
    for (int r=0;r<vis;r++){ int i=g_scroll+r; if(i>=g_nent) break;
        int ry=cy+ly+2+r*rowh;
        if (i==g_sel){ fb_fill_rect(cx+lx+2,ry,lw-20,rowh,RGB(0,0,0x80)); fb_text_t(cx+lx+6,ry+1,g_ent[i].name,RGB(0xFF,0xFF,0xFF)); }
        else fb_text_t(cx+lx+6,ry+1,g_ent[i].name,DK);
    }
    fb_clear_clip();
    /* scrollbar arrows (pixel triangles, not font glyphs which the font may lack) */
    int sbx=cx+lx+lw-16, uy=cy+ly, dyy=cy+ly+lh-16;
    cc_button(sbx,uy,16,16,"",0); cc_button(sbx,dyy,16,16,"",0);
    { int ax=sbx+8, ay=uy+8;  for(int k=0;k<4;k++) fb_fill_rect(ax-k, ay+k-2, 2*k+1, 1, RGB(0,0,0)); }   /* up */
    { int ax=sbx+8, ay=dyy+8; for(int k=0;k<4;k++) fb_fill_rect(ax-k, ay+2-k, 2*k+1, 1, RGB(0,0,0)); }   /* down */
    /* filename field + label */
    int ex,ey,ew,eh; ed_rect(cw,ch,&ex,&ey,&ew,&eh);
    fb_text_t(cx+12,cy+ey+4,"File name:",DK);
    cc_edit(cx+ex,cy+ey,ew,eh,g_name,1);
    /* buttons */
    int bx,oy,cy2,bw,bh; btn_rects(cw,ch,&bx,&oy,&cy2,&bw,&bh);
    cc_defbutton(cx+bx,cy+oy,bw,bh,g_isSave?"Save":"Open",0);
    cc_button(cx+bx,cy+cy2,bw,bh,"Cancel",0);
}

static void confirm(void){
    if (!g_name[0]) return;
    int i=0; for(;g_name[i]&&i<63;i++);           /* trim trailing spaces */
    void (*ok)(const char *) = g_onok;
    char picked[64]; for (i=0;g_name[i]&&i<63;i++) picked[i]=g_name[i]; picked[i]=0;
    DestroyWindow(g_dlg);
    if (ok) ok(picked);                            /* fire after teardown (onok may open another dialog) */
}
static void dlg_click(int rx,int ry){
    int cx,cy,cw,ch; GetClientOrigin(g_dlg,&cx,&cy,&cw,&ch); (void)cx;(void)cy;
    int lx,ly,lw,lh,rowh,vis; rects(cw,ch,&lx,&ly,&lw,&lh,&rowh,&vis);
    int sbx=lx+lw-16;
    if (rx>=sbx && rx<sbx+16){                      /* scrollbar arrows */
        if (ry>=ly && ry<ly+16){ if(g_scroll>0)g_scroll--; return; }
        if (ry>=ly+lh-16 && ry<ly+lh){ if(g_scroll+vis<g_nent)g_scroll++; return; }
    }
    if (rx>=lx && rx<lx+lw-18 && ry>=ly+2 && ry<ly+lh){   /* pick a row */
        int r=(ry-(ly+2))/rowh, i=g_scroll+r;
        if (i>=0 && i<g_nent){ g_sel=i; int k=0; for(;g_ent[i].name[k]&&k<63;k++)g_name[k]=g_ent[i].name[k]; g_name[k]=0; g_editlen=k; }
        return;
    }
    int bx,oy,cy2,bw,bh; btn_rects(cw,ch,&bx,&oy,&cy2,&bw,&bh);
    if (rx>=bx && rx<bx+bw){
        if (ry>=oy && ry<oy+bh){ confirm(); return; }
        if (ry>=cy2 && ry<cy2+bh){ DestroyWindow(g_dlg); return; }
    }
}
static void dlg_key(char c){
    if (c=='\r'||c=='\n'){ confirm(); return; }
    if (c=='\b'){ if(g_editlen>0) g_name[--g_editlen]=0; return; }
    if (c>=' ' && c<127 && g_editlen<40){ g_name[g_editlen++]=c; g_name[g_editlen]=0; }
}

/* ---- Win32 veneer: WndProc ---- */
static LRESULT CALLBACK Comdlg_WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: g_dlg = hwnd; return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps; BeginPaint(hwnd, &ps);
            int ox, oy, cw, ch; GetClientOrigin(hwnd, &ox, &oy, &cw, &ch);
            dlg_paint(ox, oy, cw, ch);
            EndPaint(hwnd, ps.hdc);
            return 0;
        }
        case WM_CHAR:        dlg_key((char)wParam); return 0;
        case WM_LBUTTONDOWN: dlg_click(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
        case WM_DESTROY:     g_dlg = 0; return 0;
        default: return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

static int open_dialog(OPENFILENAME *ofn, int isSave){
    if (!ofn) return 0;
    const char *t = ofn->title ? ofn->title : (isSave?"Save As":"Open");
    int i=0; for(;t[i]&&i<46;i++) g_title[i]=t[i]; g_title[i]=0;
    i=0; if(ofn->filter) for(;ofn->filter[i]&&i<15;i++) g_filter[i]=ofn->filter[i]; g_filter[i]=0;
    g_onok=ofn->onok; g_isSave=isSave; g_sel=-1; g_scroll=0;
    /* Save: preseed the edit with any suggested name; Open: start empty */
    g_name[0]=0; g_editlen=0;
    if (isSave && ofn->file[0]){ for(i=0;ofn->file[i]&&i<40;i++) g_name[i]=ofn->file[i]; g_name[i]=0; g_editlen=i; }
    refresh();
    int w=380,h=272;
    if (g_dlg) SetWindowText(g_dlg,g_title);
    else CreateAppWindow(((int)fb_width()-w)/2,((int)fb_height()-h)/2,w,h,g_title,Comdlg_WndProc,0);
    return 1;
}
int GetOpenFileName(OPENFILENAME *ofn){ return open_dialog(ofn,0); }
int GetSaveFileName(OPENFILENAME *ofn){ return open_dialog(ofn,1); }
