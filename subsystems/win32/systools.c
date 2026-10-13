/* Debian95 — System Tools: ScanDisk and Disk Defragmenter, as authentic Win95
 * animated progress windows. These are faithful VISUAL tools (they animate a
 * surface scan / cluster consolidation) rather than touching the live mounted
 * root, which fsck/defrag cannot safely do while it is in use. Each is a single
 * in-process app window driven by a per-frame *_tick() from the desktop loop. */
#include <debian95/win32.h>
#include <debian95/fb.h>
#include <debian95/icons.h>
#include <debian95/wm.h>

#define FACE   RGB(0xC0,0xC0,0xC0)
#define WHITE  RGB(0xFF,0xFF,0xFF)
#define DKGRAY RGB(0x80,0x80,0x80)
#define BLACK  RGB(0x00,0x00,0x00)
#define NAVY   RGB(0x00,0x00,0x80)

static unsigned lcg = 0x1234abcd;
static unsigned rnd(void){ lcg = lcg*1103515245u + 12345u; return (lcg>>16)&0x7fff; }

/* a sunken (client) 3D box */
static void box_sunken(int x,int y,int w,int h){
    fb_fill_rect(x,y,w,1,DKGRAY); fb_fill_rect(x,y,1,h,DKGRAY);
    fb_fill_rect(x,y+h-1,w,1,WHITE); fb_fill_rect(x+w-1,y,1,h,WHITE);
}
static void cell3d(int x,int y,int s,u32 c){
    fb_fill_rect(x,y,s,s,c);
    fb_fill_rect(x,y,s,1,WHITE); fb_fill_rect(x,y,1,s,WHITE);
    fb_fill_rect(x,y+s-1,s,1,DKGRAY); fb_fill_rect(x+s-1,y,1,s,DKGRAY);
}
static void progbar(int x,int y,int w,int h,int pct){
    box_sunken(x,y,w,h);
    int fw = (w-4)*pct/100; if(fw<0)fw=0; if(fw>w-4)fw=w-4;
    /* Win95 progress = a row of blue blocks */
    for(int bx=0; bx+6<=fw; bx+=8) fb_fill_rect(x+2+bx,y+2,6,h-4,NAVY);
}

/* ===================== ScanDisk ===================== */
#define SD_COLS 28
#define SD_ROWS 10
static HWND g_sd; static int sd_phase, sd_pos, sd_done; static u32 sd_last;
/* phases: 0 files, 1 folders, 2 FAT, 3 surface(grid), 4 complete */
static const char *sd_msg[] = {
    "Checking files and folders...","Checking files and folders...",
    "Checking the file allocation table...","Scanning disk surface...","Complete."
};
static u8 sd_cell[SD_COLS*SD_ROWS];

static void scandisk_paint(int ox,int oy,int cw,int ch){
    fb_fill_rect(ox,oy,cw,ch,FACE);
    fb_text_t(ox+12,oy+10,"ScanDisk is checking Drive C:",BLACK);
    fb_text_t(ox+12,oy+30,sd_msg[sd_phase],BLACK);
    int gx=ox+12, gy=oy+52, s=10;
    box_sunken(gx-3,gy-3,SD_COLS*s+5,SD_ROWS*s+5);
    for(int r=0;r<SD_ROWS;r++)for(int c=0;c<SD_COLS;c++){
        int i=r*SD_COLS+c; u32 col = sd_cell[i]==2?RGB(0xFF,0xFF,0x00): sd_cell[i]==1?RGB(0x00,0xA0,0x00):FACE;
        cell3d(gx+c*s,gy+r*s,s-1,col);
    }
    int py=gy+SD_ROWS*s+14;
    int pct = sd_phase<3?sd_phase*20: sd_phase==3? 60+sd_pos*40/(SD_COLS*SD_ROWS): 100;
    progbar(ox+12,py,cw-24,16,pct);
    char pc[8]; int j=0,v=pct; if(!v)pc[j++]='0'; char t[4]; int k=0; while(v){t[k++]='0'+v%10;v/=10;} while(k)pc[j++]=t[--k]; pc[j++]='%'; pc[j]=0;
    fb_text_t(ox+cw/2-8,py+2,pc,WHITE);
    if(sd_done) fb_text_t(ox+12,py+26,"ScanDisk did not find any errors.",RGB(0x00,0x60,0x00));
}
static LRESULT CALLBACK SD_Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    (void)w;(void)l;
    switch(m){
        case WM_CREATE: g_sd=h; SetWindowIcon(h,IC_APP);
            sd_phase=0; sd_pos=0; sd_done=0; sd_last=0;
            for(int i=0;i<SD_COLS*SD_ROWS;i++) sd_cell[i]=0;
            return 0;
        case WM_PAINT:{ PAINTSTRUCT ps; BeginPaint(h,&ps); int ox,oy,cw,ch;
            GetClientOrigin(h,&ox,&oy,&cw,&ch); scandisk_paint(ox,oy,cw,ch); EndPaint(h,ps.hdc); return 0; }
        case WM_DESTROY: g_sd=0; return 0;
        default: return DefWindowProc(h,m,w,l);
    }
}
int scandisk_tick(void){
    if(!g_sd||sd_done) return 0;
    u32 now=wm_now_ms(); if(now-sd_last<70) return 0; sd_last=now;
    if(sd_phase<3){ sd_phase++; return 1; }                 /* early phases: step every tick */
    /* surface scan: light up cells left-to-right, a few per tick */
    for(int n=0;n<3 && sd_pos<SD_COLS*SD_ROWS;n++){ sd_cell[sd_pos]=1; sd_pos++; }
    if(sd_pos<SD_COLS*SD_ROWS){ sd_cell[sd_pos]=2; return 1; }   /* current = yellow head */
    sd_phase=4; sd_done=1;
    MessageBoxW32(g_sd,"ScanDisk has finished checking Drive C:.\n\nScanDisk did not find any errors on this drive.","ScanDisk Results",MB_ICONINFO);
    return 1;
}
void scandisk_open(void){ if(g_sd) return; lcg^=wm_now_ms();
    CreateAppWindow(160,110,SD_COLS*10+40,SD_ROWS*10+150,"ScanDisk - Drive C:",SD_Proc,0); }

/* ===================== Disk Defragmenter ===================== */
#define DF_COLS 40
#define DF_ROWS 14
#define DF_N (DF_COLS*DF_ROWS)
enum { E=0, USED=1, READ=2, DONE_=3 };
static HWND g_df; static u8 df_cell[DF_N]; static int df_used, df_wp, df_rp, df_hi, df_done; static u32 df_last;

static void defrag_init(void){
    df_used=0; df_wp=0; df_rp=0; df_hi=-1; df_done=0; df_last=0;
    for(int i=0;i<DF_N;i++){ int v = (rnd()%10 < 6); df_cell[i]= v?USED:E; if(v)df_used++; }
    /* punch holes so there is something to consolidate */
    for(int i=0;i<DF_N;i++) if(df_cell[i]==USED && (rnd()%4==0)){ df_cell[i]=E; df_used--; }
}
static void defrag_paint(int ox,int oy,int cw,int ch){
    fb_fill_rect(ox,oy,cw,ch,FACE);
    fb_text_t(ox+12,oy+10, df_done?"Defragmentation of Drive C: is complete.":"Defragmenting Drive C:...",BLACK);
    int gx=ox+12, gy=oy+34, s=9;
    box_sunken(gx-3,gy-3,DF_COLS*s+5,DF_ROWS*s+5);
    for(int r=0;r<DF_ROWS;r++)for(int c=0;c<DF_COLS;c++){
        int i=r*DF_COLS+c; u32 col = i==df_hi?RGB(0xFF,0xFF,0x00):
            df_cell[i]==DONE_?NAVY: df_cell[i]==USED?RGB(0x30,0x60,0xD0): df_cell[i]==READ?RGB(0xC0,0x00,0x00):WHITE;
        cell3d(gx+c*s,gy+r*s,s-1,col);
    }
    int py=gy+DF_ROWS*s+12;
    int pct = df_used? df_wp*100/df_used:100; if(df_done)pct=100;
    progbar(ox+12,py,cw-24,16,pct);
    char pc[16]="            "; /* "NN% Complete" */
    int v=pct,k=0,j=0; char t[4]; if(!v)t[k++]='0'; while(v){t[k++]='0'+v%10;v/=10;} while(k)pc[j++]=t[--k];
    const char*sfx="% Complete"; for(int q=0;sfx[q];q++)pc[j++]=sfx[q]; pc[j]=0;
    fb_text_t(ox+cw/2-30,py+2,pc,WHITE);
    /* legend */
    int ly=py+26; cell3d(ox+12,ly,8,RGB(0x30,0x60,0xD0)); fb_text_t(ox+26,ly,"Used",BLACK);
    cell3d(ox+80,ly,8,NAVY); fb_text_t(ox+94,ly,"Optimized",BLACK);
    cell3d(ox+170,ly,8,WHITE); fb_text_t(ox+184,ly,"Free",BLACK);
}
static LRESULT CALLBACK DF_Proc(HWND h,UINT m,WPARAM w,LPARAM l){
    (void)w;(void)l;
    switch(m){
        case WM_CREATE: g_df=h; SetWindowIcon(h,IC_APP); defrag_init(); return 0;
        case WM_PAINT:{ PAINTSTRUCT ps; BeginPaint(h,&ps); int ox,oy,cw,ch;
            GetClientOrigin(h,&ox,&oy,&cw,&ch); defrag_paint(ox,oy,cw,ch); EndPaint(h,ps.hdc); return 0; }
        case WM_DESTROY: g_df=0; return 0;
        default: return DefWindowProc(h,m,w,l);
    }
}
int defrag_tick(void){
    if(!g_df||df_done) return 0;
    u32 now=wm_now_ms(); if(now-df_last<55) return 0; df_last=now;
    /* advance the write front past already-placed blocks */
    while(df_wp<DF_N && df_cell[df_wp]==USED){ df_cell[df_wp]=DONE_; df_wp++; }
    if(df_wp>=DF_N){ df_done=1; df_hi=-1;
        MessageBoxW32(g_df,"Defragmentation of Drive C: is complete.","Disk Defragmenter",MB_ICONINFO); return 1; }
    /* find the next used block to pull forward */
    if(df_rp<=df_wp) df_rp=df_wp+1;
    while(df_rp<DF_N && df_cell[df_rp]!=USED) df_rp++;
    if(df_rp>=DF_N){ /* nothing left to move; pack remaining */
        int any=0; for(int i=df_wp;i<DF_N;i++) if(df_cell[i]==USED){any=1;break;}
        if(!any){ df_done=1; df_hi=-1; MessageBoxW32(g_df,"Defragmentation of Drive C: is complete.","Disk Defragmenter",MB_ICONINFO); return 1; }
        df_rp=df_wp+1; return 1;
    }
    df_cell[df_rp]=E; df_cell[df_wp]=DONE_; df_hi=df_wp; df_wp++;
    return 1;
}
void defrag_open(void){ if(g_df) return; lcg^=wm_now_ms();
    CreateAppWindow(140,90,DF_COLS*9+40,DF_ROWS*9+150,"Disk Defragmenter - Drive C:",DF_Proc,0); }
