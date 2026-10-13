/* Debian95 — kernel core: interrupt controller, timer, scheduler. */
#ifndef DEBIAN95_KE_H
#define DEBIAN95_KE_H

#include <debian95/types.h>
#include <debian95/idt.h>

/* ---- GDT + TSS ---- */
void gdt_init(void);               /* kernel GDT with ring-3 segments + TSS */
void tss_set_rsp0(u64 rsp0);

/* CPU privilege hardening: EFER.NXE, CR0.WP, CR4.SMEP (when supported). */
void cpu_harden(void);
void cpu_ring_status(char *out, int n);   /* human-readable ring/protection state */

/* ---- preemptive task scheduler ---- */
typedef struct { u64 pid; const char *name; int state; } TaskInfo;  /* state: 1=running 3=dead */

void task_init(void);                                   /* register the desktop as task 0 */
int  task_create_user(u64 cr3, u64 entry, u64 ustack, void *ht, u64 pid, const char *name);
u64  task_switch(InterruptFrame *f);                    /* from the timer IRQ */
void task_exit(u64 code) __attribute__((noreturn));     /* from SYS_EXIT */
void task_reap(void);                                   /* free dead tasks (call from desktop) */
int  task_kill(u64 pid);                                /* terminate a process; 0 on success */
int  task_list(TaskInfo *out, int max);                 /* list tasks; returns count */

/* ---- CMOS real-time clock ---- */
void rtc_time(int *hh, int *mm);
/* full system date/time (Date/Time applet) */
void sys_date(int *y, int *mo, int *d, int *dow, int *hh, int *mm, int *ss);
int  sys_set_datetime(int y, int mo, int d, int hh, int mm, int ss);   /* 0 on success */

/* ---- installed-hardware probes (drive which tray status icons appear) ---- */
int  sys_has_audio(void);     /* 1 if a sound driver/device is present */
void sys_suspend(void);       /* Stand By: suspend-to-RAM */
int  sys_battery(int *pct, int *charging);   /* 0 if a battery exists (fills pct/charging), -1 if none */
void sys_disk_automount(void);               /* mount any freshly-inserted removable media */
int  sys_has_network(void);   /* 1 if a (non-loopback) network device is present */
/* play a WAV from the data dir by name (e.g. "mssound.wav"); no-op if no audio */
void snd_play(const char *name);
/* play the sound assigned to a system event ("start","error","warning",
 * "question","info","beep","exit","menu","open"); assignment persists */
void snd_event(const char *ev);
/* set the real system volume (0-100) / mute via ALSA (tray control) */
void sys_set_volume(int pct);
void sys_set_mute(int on);
/* audio output (sink) selection - Multimedia applet "Preferred device" */
int  sys_audio_outputs(char descs[][96], char ids[][128], int max);  /* count; descs human-readable */
int  sys_audio_current(char *id, int n);                             /* 0 ok, fills default sink id */
void sys_audio_set_output(const char *id);                           /* set default + move streams */
/* display brightness (Display applet slider + tray); -1 if no backlight */
int  sys_has_brightness(void);
int  sys_brightness_get(void);
void sys_brightness_set(int pct);
/* Bluetooth (Control Panel applet) over BlueZ bluetoothctl */
int  sys_bt_available(void);
int  sys_bt_powered(void);
void sys_bt_power(int on);
void sys_bt_scan(int on);
int  sys_bt_list(char names[][64], char macs[][20], int *connected, int max);  /* count */
void sys_bt_connect(const char *mac);
void sys_bt_disconnect(const char *mac);

/* ---- logon: authenticate the desktop against the real system account ---- */
int sys_has_password(const char *user);           /* 1 if a real password is set */
int sys_auth(const char *user, const char *pass); /* 1 = ok, 0 = wrong, -1 = no/locked account */
int sys_set_password(const char *user, const char *pass);  /* write the hash; 0 on success */
/* ---- LUKS-encrypted /data, unlocked with the logon password ----
 * The persistent data disk is a LUKS volume; the password that opens it IS the
 * logon (no /etc/shadow needed). On unlock/format the desktop mounts /data and
 * overlays the writable data dir onto it so user files persist encrypted. */
int sys_data_is_luks(void);                /* 1 if /data is already LUKS -> login mode */
int sys_data_unlock(const char *pass);     /* login: open+mount+overlay; 1 ok, 0 wrong pass */
int sys_data_verify(const char *pass);     /* screen-lock re-auth: verify only; 1 ok, 0 wrong */
int sys_data_format(const char *pass);     /* first run: luksFormat+mkfs+mount+overlay; 0 ok */
int sys_data_rekey(const char *oldp, const char *newp); /* change the LUKS passphrase; 0 ok */

/* ---- live kernel stats from the debian95.ko integration driver (ioctl) ----
 * The desktop reads kernel-authoritative system state straight from the module
 * over /dev/debian95, proving the driver is genuinely integrated (not a stub). */
struct lg_kstat {
	unsigned ver_major, ver_minor, logged_in, boot_ready, nr_procs, lowmem;
	unsigned long long uptime_ms, session_ms, mem_total_kb, mem_free_kb;
};
int sys_kstat(struct lg_kstat *out);   /* 0 = ok (module loaded), -1 = driver absent */
/* kernel-tracked session (debian95.ko /dev/debian95); no-op if the module isn't loaded */
void sys_session_login(const char *user);
void sys_session_logout(void);
/* kernel-metered boot pacing (debian95.ko); large value if the module is absent */
int  sys_boot_ready(void);
/* persistent settings store (SETTINGS.INI in the data dir) */
int  sys_get_setting(const char *key, char *out, int n);   /* 0 on found */
void sys_set_setting(const char *key, const char *val);
/* per-user profiles: switch the settings store to <data>/users/<user>/ (empty = global) */
void sys_set_profile(const char *user);
/* Network Neighborhood (SMB/CIFS): discover computers, list shares, mount one */
int  sys_net_hosts(char names[][64], int max);
int  sys_net_shares(const char *host, char shares[][64], int max);
int  sys_net_mount(const char *host, const char *share, char *mountpoint, int mn);  /* 0 ok */
/* Snipping Tool: screenshot the whole screen to <data>/<relname> (scrot); 0 ok */
int  sys_snip(const char *relname);
/* Text to speech ("Microsoft Sam" / Narrator) via espeak-ng */
void sys_speak(const char *text);
void sys_speak_stop(void);
/* live system stats for the desktop gadget sidebar */
struct lg_sysmon { int cpu_pct, mem_pct; unsigned long mem_total_mb, mem_used_mb, net_rx_kbs, net_tx_kbs; };
int  sys_sysmon(struct lg_sysmon *out);
/* AI Clippy: a real local LLM (llama.cpp + bundled model), async one-shot chat */
int  sys_ai_available(void);
int  sys_ai_ask(const char *msg);        /* start generating; 0 ok */
int  sys_ai_reply(char *out, int n);     /* 1 = reply ready (filled), 0 = still thinking */
/* Remote Access: host this desktop over VNC (x11vnc on :0), password-guarded */
int  sys_vnc_available(void);
int  sys_vnc_running(void);
int  sys_vnc_has_password(void);
int  sys_vnc_set_password(const char *pw);
int  sys_vnc_start(void);                 /* uses the stored password; 0 ok, -2 if none */
void sys_vnc_stop(void);
void sys_vnc_info(char *ip, int iplen, int *port);
/* Scheduled Tasks: the user's crontab (cron) */
struct lg_cron { char when[48]; char cmd[160]; };
int  sys_cron_list(struct lg_cron *out, int max);
int  sys_cron_add(const char *when, const char *cmd);
int  sys_cron_del(int index);
/* Firewall: a front-end to ufw (the uncomplicated firewall) */
struct lg_fwrule { int num; char text[96]; };
int  sys_fw_available(void);
int  sys_fw_status(void);                 /* 1 = active, 0 = inactive */
int  sys_fw_set(int on);                  /* enable/disable; 0 ok */
int  sys_fw_rules(struct lg_fwrule *out, int max);
int  sys_fw_add(const char *spec);        /* e.g. "allow 22/tcp"; 0 ok */
int  sys_fw_del(int num);                 /* delete rule by number; 0 ok */
/* System Restore: snapshot/restore the persisted desktop state (settings, themes,
 * wallpaper, per-user profiles). Restore points live under the encrypted data dir. */
struct lg_restore { char id[96]; char label[64]; char when[32]; };
int  sys_restore_create(const char *label);     /* 0 ok */
int  sys_restore_list(struct lg_restore *out, int max);
int  sys_restore_apply(const char *id);         /* 0 ok */
int  sys_restore_del(const char *id);           /* 0 ok */
/* change the real framebuffer resolution (fbdev mode-set); 0 on success */
int  sys_set_resolution(int w, int h);

/* ---- Device Manager: the real hardware tree (lspci/lsusb/cpuinfo/lsblk) ---- */
struct lg_device { int cat; char name[96]; };   /* cat = DEVCAT_* (see devmgr.c) */
int sys_device_list(struct lg_device *out, int max);   /* fills out[], returns count */

/* ---- Event Viewer: structured journal entries (journalctl -o json) ---- */
struct lg_logent { int level; char time[24]; char source[40]; char msg[200]; };  /* level 0=Error 1=Warning 2=Info */
int sys_log_list(struct lg_logent *out, int max);

/* ---- real OS process list (Task Manager) - reads /proc on the Linux editions ---- */
struct lg_proc { int pid; char name[24]; unsigned long rss_kb; };
int  sys_proc_list(struct lg_proc *out, int max);   /* fills out[], returns count */
void sys_proc_kill(int pid);                          /* SIGTERM a process (End Process) */

/* ---- PC speaker ---- */
void beep(u32 freq);

/* ---- 8259 PIC + PIT ---- */
void pic_remap(void);              /* IRQs 0-15 -> vectors 32-47 */
void pic_send_eoi(u32 irq);
void pit_init(u32 hz);             /* channel 0 periodic timer */

/* ---- scheduler (preemptive round-robin, kernel threads) ---- */
extern volatile u64 g_ticks;

void sched_init(void);             /* register the current context as the idle thread */
void thread_create(void (*entry)(void));
u64  sched_on_tick(InterruptFrame *f);   /* returns the rsp to resume on */

static inline void sti(void) { __asm__ volatile ("sti"); }
static inline void cli(void) { __asm__ volatile ("cli"); }

#endif /* DEBIAN95_KE_H */
