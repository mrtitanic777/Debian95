/* Debian95 — minimal FAT32 reader (mount, list root, read file by 8.3 name). */
#ifndef DEBIAN95_FAT_H
#define DEBIAN95_FAT_H

#include <debian95/types.h>
#include <debian95/io.h>

typedef struct {
    DeviceObject *dev;
    u32 fat_start;      /* LBA of first FAT */
    u32 data_start;     /* LBA of the first data cluster (cluster 2) */
    u32 sectors_per_cluster;
    u32 root_cluster;
    u32 num_fats;
    u32 fat_sectors;    /* sectors per FAT */
} FatVolume;

int fat_mount(DeviceObject *dev, FatVolume *vol);   /* 0 on success */
void fat_list_root(FatVolume *vol);
int fat_read_file(FatVolume *vol, const char *name8_3, void *buf, u32 maxlen); /* bytes read, -1 if not found */
int fat_read_alloc(FatVolume *vol, const char *name8_3, u8 **out, u32 *size);  /* allocs buffer; 0 on success */
int fat_list(FatVolume *vol, char names[][16], u32 *sizes, int max);           /* returns entry count */
int fat_write_file(FatVolume *vol, const char *name8_3, const u8 *data, u32 size); /* create/overwrite; 0 ok */
int fat_delete_file(FatVolume *vol, const char *name8_3);                          /* free chain + mark 0xE5; 0 ok */

#endif /* DEBIAN95_FAT_H */
