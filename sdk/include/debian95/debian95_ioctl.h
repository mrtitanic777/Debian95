/* Debian95 kernel-integration ioctl ABI (userspace side).
 * MUST stay byte-compatible with struct debian95_stat + the ioctl codes in
 * kernel/debian95_mod/debian95.c. The desktop opens /dev/debian95 and issues
 * DEBIAN95_IOC_STAT to read live, kernel-authoritative system stats. */
#ifndef DEBIAN95_IOCTL_H
#define DEBIAN95_IOCTL_H

#include <stdint.h>
#include <sys/ioctl.h>

struct debian95_stat {
	uint32_t ver_major, ver_minor;
	uint64_t uptime_ms;        /* system uptime */
	uint64_t session_ms;       /* since login (0 if logged out) */
	uint32_t logged_in;
	uint32_t boot_ready;
	uint64_t mem_total_kb, mem_free_kb;
	uint32_t nr_procs;
	uint32_t flags;            /* DEBIAN95_STAT_* */
};
#define DEBIAN95_STAT_LOWMEM  0x1
#define DEBIAN95_IOC_MAGIC    'L'
#define DEBIAN95_IOC_STAT     _IOR(DEBIAN95_IOC_MAGIC, 1, struct debian95_stat)

#endif /* DEBIAN95_IOCTL_H */
