/* Debian95 — memory manager: physical frames, virtual mapping, kernel heap. */
#ifndef DEBIAN95_MM_H
#define DEBIAN95_MM_H

#include <debian95/types.h>
#include <debian95/boot.h>

#define PAGE_SIZE        0x1000ull
#define PAGE_MASK        (PAGE_SIZE - 1)
#define PAGE_ALIGN_DOWN(x) ((x) & ~PAGE_MASK)
#define PAGE_ALIGN_UP(x)   (((x) + PAGE_MASK) & ~PAGE_MASK)

/* Canonical higher-half regions the kernel installs (own page tables). */
#define KERNEL_VMA       0xFFFFFFFF80000000ull   /* kernel image (PML4[511]) */
#define HHDM_BASE        0xFFFF800000000000ull   /* direct map of all RAM (PML4[256]) */
#define HEAP_BASE        0xFFFFC00000000000ull   /* kernel heap (PML4[384]) */

/* Page-table entry flags. */
#define PTE_P            0x001ull   /* present */
#define PTE_W            0x002ull   /* writable */
#define PTE_US           0x004ull   /* user-accessible */
#define PTE_PS           0x080ull   /* page size (2 MiB at PD level) */
#define PTE_NX           0x8000000000000000ull   /* no-execute (needs EFER.NXE) */

/* Physical <-> virtual through the direct map. g_hhdm_base is 0 (identity)
 * until vmm_init installs the direct map, then HHDM_BASE. */
extern u64 g_hhdm_base;
static inline void *phys_to_virt(u64 pa) { return (void *)(pa + g_hhdm_base); }

/* Physical address of a kernel-image (higher-half) address. The kernel image is
 * mapped contiguously from phys 0x200000, so this is exact for .bss/.data — used
 * for DMA buffers. (Do NOT use for HHDM/heap addresses.) */
#define KERNEL_LMA 0x200000ull
static inline u64 kv2p(void *va) { return (u64)va - KERNEL_VMA + KERNEL_LMA; }

/* ---- physical memory manager (frame allocator) ---- */
void pmm_init(const LoaderBlock *lb);
u64  pmm_alloc_frame(void);     /* returns physical addr of a zeroed 4 KiB frame, 0 = OOM */
void pmm_free_frame(u64 pa);
u64  pmm_free_frames(void);
u64  pmm_total_frames(void);

/* ---- virtual memory manager ---- */
void vmm_init(const LoaderBlock *lb);          /* build + install kernel page tables */
void vmm_map(u64 va, u64 pa, u64 flags);       /* map one 4 KiB page in kernel space */
void vmm_map_at(u64 pml4_pa, u64 va, u64 pa, u64 flags);  /* map in a given space */
/* Adjust protection of an existing 4 KiB page (W^X): writable/executable flags. */
void vmm_protect(u64 pml4_pa, u64 va, int writable, int executable);
u64  vmm_create_address_space(void);           /* new PML4 sharing the kernel higher-half */
void vmm_switch_space(u64 pml4_pa);            /* load CR3 */
u64  vmm_kernel_pml4(void);                    /* the kernel address space */
void vmm_destroy_address_space(u64 pml4_pa);   /* free a process's user mappings + PML4 */

/* ---- kernel heap ---- */
void  heap_init(void);
void *kmalloc(usize n);
void  kfree(void *p);

#endif /* DEBIAN95_MM_H */
