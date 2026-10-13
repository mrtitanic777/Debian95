/* Debian95 — MPEG-1 video playback via pl_mpeg (MIT, Dominic Szablewski).
 *
 * Decodes a raw MPEG-1 video elementary stream (or PS) frame-by-frame into
 * 0x00RRGGBB pixels for the Media Player. pl_mpeg is dropped in with
 * PLM_NO_STDIO and PLM_MALLOC routed to the kernel heap; its float paths run
 * on the now-enabled AArch64 FPU. x86 (kernel built -mno-sse) gets stubs.
 */
#include <debian95/types.h>
#include <debian95/mm.h>
#include <debian95/nt.h>       /* FsWriteFile for the extracted audio track */

/* Build the real pl_mpeg decoder wherever floating point / SSE is available:
 * the freestanding ARM64 kernel (has NEON) and any hosted Linux userspace
 * (x86-64 dev host + the aarch64 Buildroot desktop). Only the freestanding x86
 * kernel — no SSE in ring 0 — falls back to the stub below. */
#if defined(__aarch64__) || defined(__linux__)
/* the IRQ-mask around the decode is an EL1-only kernel guard; in Linux
 * userspace (EL0) the scheduler handles preemption and msr would fault. */
#if defined(__aarch64__) && !defined(__linux__)
#define MPEG_KERNEL_IRQ_GUARD 1
#endif

#include <stddef.h>  /* size_t, before pl_mpeg.h uses it */
#include <stdint.h>  /* uint8_t/uint32_t used across the pl_mpeg API */
#include <math.h>    /* shim: declares round() before pl_mpeg.h uses it */

/* tiny libc pieces pl_mpeg needs beyond klib's memcpy/memset — freestanding
 * only; hosted Linux userspace gets these from libc (defining them collides). */
#ifndef __linux__
void *memmove(void *d, const void *s, unsigned long n) {
    unsigned char *dd = (unsigned char *)d; const unsigned char *ss = (const unsigned char *)s;
    if (dd < ss) for (unsigned long i = 0; i < n; i++) dd[i] = ss[i];
    else         for (unsigned long i = n; i > 0; i--) dd[i-1] = ss[i-1];
    return d;
}
int abs(int x) { return x < 0 ? -x : x; }
double round(double x) { return (double)(long long)(x < 0 ? x - 0.5 : x + 0.5); }
#endif
static void *plm_realloc_stub(void *p, unsigned long s) { (void)p; (void)s; return 0; }  /* fixed-mem buffer never grows */

#define PL_MPEG_IMPLEMENTATION
#define PLM_NO_STDIO
#define PLM_MALLOC(sz)     kmalloc((u32)(sz))
#define PLM_FREE(p)        kfree(p)
#define PLM_REALLOC(p, sz) plm_realloc_stub((p), (unsigned long)(sz))
#include "pl_mpeg.h"

static plm_t       *g_plm;      /* full demuxer, for muxed SYSTEM streams (pack header 0xBA) */
static plm_buffer_t *g_buf;     /* raw video ES path (0xB3): plm_video directly */
static plm_video_t *g_vid;
static int g_vw, g_vh;

int mpeg_open(u8 *data, u32 len, int *w, int *h) {
    int sys = len >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] == 0xBA;
    plm_frame_t *fr;
    if (sys) {                                           /* system stream: demux video (+audio elsewhere) */
        g_plm = plm_create_with_memory(data, (size_t)len, 0);
        if (!g_plm) return -1;
        plm_set_audio_enabled(g_plm, 0);
        fr = plm_decode_video(g_plm);
        if (!fr) { plm_destroy(g_plm); g_plm = 0; return -1; }
        g_vw = fr->width; g_vh = fr->height; *w = g_vw; *h = g_vh;
        plm_rewind(g_plm); return 0;
    }
    g_buf = plm_buffer_create_with_memory(data, (size_t)len, 0);   /* raw video ES */
    if (!g_buf) return -1;
    g_vid = plm_video_create_with_buffer(g_buf, 1);
    if (!g_vid) { g_buf = 0; return -1; }
    fr = plm_video_decode(g_vid);
    if (!fr) { plm_video_destroy(g_vid); g_vid = 0; g_buf = 0; return -1; }
    g_vw = fr->width; g_vh = fr->height; *w = g_vw; *h = g_vh;
    plm_video_rewind(g_vid);
    return 0;
}
int mpeg_next(u32 *rgb) {
    if (!g_plm && !g_vid) return 0;
    /* Decode atomically: mask IRQs so a preemptive context switch can't land
     * mid-frame and disturb NEON/reference state during the decode. */
#ifdef MPEG_KERNEL_IRQ_GUARD
    __asm__ volatile("msr daifset, #2" ::: "memory");
#endif
    plm_frame_t *fr = g_plm ? plm_decode_video(g_plm) : plm_video_decode(g_vid);
    if (fr) plm_frame_to_bgra(fr, (uint8_t *)rgb, g_vw * 4);   /* BGRA bytes == 0x00RRGGBB u32 */
#ifdef MPEG_KERNEL_IRQ_GUARD
    __asm__ volatile("msr daifclr, #2" ::: "memory");
#endif
    return fr ? 1 : 0;
}
void mpeg_rewind(void) { if (g_plm) plm_rewind(g_plm); else if (g_vid) plm_video_rewind(g_vid); }
void mpeg_close(void)  { if (g_plm) { plm_destroy(g_plm); g_plm = 0; } if (g_vid) { plm_video_destroy(g_vid); g_vid = 0; } g_buf = 0; g_vw = g_vh = 0; }

static void wr32(u8 *p, u32 v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void wr16(u8 *p, u16 v) { p[0]=v; p[1]=v>>8; }
/* Decode the MPEG's audio track to a 16-bit stereo WAV written via FsWriteFile.
 * Returns 0 if audio was written, -1 if the file has no audio. Independent of
 * the video decoder (own plm_t on the same bytes, free_when_done=0). */
int mpeg_audio_to_wav(u8 *data, u32 len, const char *wavpath) {
    plm_t *plm = plm_create_with_memory(data, (size_t)len, 0);
    if (!plm) return -1;
    plm_set_video_enabled(plm, 0);
    if (plm_get_num_audio_streams(plm) < 1) { plm_destroy(plm); return -1; }
    plm_set_audio_enabled(plm, 1);
    int rate = plm_get_samplerate(plm); if (rate <= 0) rate = 44100;
    u32 cap = 44 + 4u * 1024 * 1024;              /* WAV header + ~11s stereo @44.1k */
    u8 *out = (u8 *)kmalloc(cap); if (!out) { plm_destroy(plm); return -1; }
    u32 n = 44;                                   /* PCM starts after the 44-byte header */
    plm_samples_t *s;
    while ((s = plm_decode_audio(plm)) != 0) {
        for (unsigned i = 0; i < s->count * 2; i++) {
            float f = s->interleaved[i]; if (f > 1) f = 1; if (f < -1) f = -1;
            int v = (int)(f * 32767);
            if (n + 2 <= cap) { out[n++] = (u8)(v & 0xFF); out[n++] = (u8)((v >> 8) & 0xFF); }
        }
        if (n + 8 >= cap) break;
    }
    plm_destroy(plm);
    if (n <= 44) { kfree(out); return -1; }
    u32 datalen = n - 44, ch = 2, bits = 16, byterate = (u32)rate * ch * bits / 8;
    out[0]='R';out[1]='I';out[2]='F';out[3]='F'; wr32(out+4, n - 8);
    out[8]='W';out[9]='A';out[10]='V';out[11]='E';
    out[12]='f';out[13]='m';out[14]='t';out[15]=' '; wr32(out+16, 16); wr16(out+20, 1);
    wr16(out+22, (u16)ch); wr32(out+24, (u32)rate); wr32(out+28, byterate);
    wr16(out+32, (u16)(ch*bits/8)); wr16(out+34, (u16)bits);
    out[36]='d';out[37]='a';out[38]='t';out[39]='a'; wr32(out+40, datalen);
    int rc = FsWriteFile(wavpath, out, n);
    kfree(out);
    return rc == 0 ? 0 : -1;
}

#else  /* x86 kernel: no SSE, so pl_mpeg's float paths can't be built here */

int  mpeg_open(u8 *data, u32 len, int *w, int *h) { (void)data;(void)len;(void)w;(void)h; return -1; }
int  mpeg_next(u32 *rgb) { (void)rgb; return 0; }
int  mpeg_audio_to_wav(u8 *d, u32 l, const char *p) { (void)d;(void)l;(void)p; return -1; }
void mpeg_rewind(void) {}
void mpeg_close(void) {}

#endif
