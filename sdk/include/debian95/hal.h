/* Debian95 — hardware abstraction seam for the arch-agnostic executive. Each
 * architecture implements these; the shared kernel code (e.g. the ELF loader)
 * calls them instead of touching arch-specific paging/cache details. */
#ifndef DEBIAN95_HAL_H
#define DEBIAN95_HAL_H

#include <debian95/types.h>

u64  pmm_alloc_frame(void);                 /* a zeroed physical frame, 0 = OOM */

/* ---- ELF-loader seam ---- */
u16  hal_elf_machine(void);                 /* expected ELF e_machine for this arch */
void hal_map_user_page(u64 space, u64 page, u64 pa);        /* map a RW user page */
u8  *hal_user_dst(u64 pa);                  /* a writable kernel VA for that frame */
void hal_finalize_page(u64 space, u64 page, u64 pa, int w, int x);  /* set final W^X */
void hal_sync_icache(void);                 /* make freshly written code executable */

#endif /* DEBIAN95_HAL_H */
