/* Debian95 — Loader Block: the hand-off structure the bootloader passes the kernel.
 *
 * The stage-2 bootloader fills this at physical 0x9000 and passes its address in
 * RDI to the kernel entry point. All addresses are PHYSICAL; the kernel reads them
 * through the identity map that stage 2 installs for the low 1 GiB.
 *
 * This layout MUST stay byte-for-byte in sync with boot/x86/stage2.asm.
 */
#ifndef DEBIAN95_BOOT_H
#define DEBIAN95_BOOT_H

#include <debian95/types.h>

#define LOADER_BLOCK_ADDR  0x9000u
#define LOADER_MAGIC       0x4C474E31u   /* 'LGN1' */
#define E820_ARRAY_ADDR    0x9100u

/* One BIOS INT 15h E820 memory-map entry (24 bytes). */
typedef struct {
    u64 base;
    u64 length;
    u32 type;        /* 1 = usable RAM */
    u32 acpi;        /* extended attributes */
} E820Entry;

#define E820_TYPE_USABLE   1

/* Filled by stage 2 at LOADER_BLOCK_ADDR. */
typedef struct {
    u32 magic;         /* LOADER_MAGIC */
    u32 e820_count;    /* number of E820Entry records */
    u64 e820_addr;     /* physical address of the E820 array */
    u8  boot_drive;    /* BIOS drive number we booted from */
    u8  reserved[7];
    u64 fb_addr;       /* linear framebuffer physical address (0 = none) */
    u32 fb_width;
    u32 fb_height;
    u32 fb_pitch;      /* bytes per scanline */
    u32 fb_bpp;
} LoaderBlock;

#endif /* DEBIAN95_BOOT_H */
