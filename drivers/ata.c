/* Debian95 — ATA PIO disk driver (LBA28, primary bus 0x1F0). Simple polled reads,
 * sufficient for booting/QEMU. drive 0 = master, 1 = slave. */
#include <debian95/io.h>
#include <debian95/rtl.h>

#define ATA_IO      0x1F0
#define ATA_DATA    (ATA_IO + 0)
#define ATA_SECCNT  (ATA_IO + 2)
#define ATA_LBA0    (ATA_IO + 3)
#define ATA_LBA1    (ATA_IO + 4)
#define ATA_LBA2    (ATA_IO + 5)
#define ATA_DRIVE   (ATA_IO + 6)
#define ATA_STATUS  (ATA_IO + 7)
#define ATA_CMD     (ATA_IO + 7)

#define ST_BSY  0x80
#define ST_DRQ  0x08
#define ST_ERR  0x01
#define CMD_READ_SECTORS  0x20
#define CMD_WRITE_SECTORS 0x30
#define CMD_FLUSH_CACHE   0xE7
#define SPIN_MAX 10000000u

static int wait_clear_bsy(void) {
    for (u32 i = 0; i < SPIN_MAX; i++) {
        u8 s = inb(ATA_STATUS);
        if (s == 0xFF) return -1;            /* no drive present */
        if (!(s & ST_BSY)) return 0;
    }
    return -1;
}

static int wait_drq(void) {
    for (u32 i = 0; i < SPIN_MAX; i++) {
        u8 s = inb(ATA_STATUS);
        if (s & ST_ERR) return -1;
        if (s & ST_DRQ) return 0;
    }
    return -1;
}

static int ata_read_impl(DeviceObject *dev, u64 lba, u32 count, void *buf) {
    u32 drive = *(u32 *)dev->ext;
    u16 *out = (u16 *)buf;

    if (wait_clear_bsy() != 0) return -1;
    outb(ATA_DRIVE, (u8)(0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCNT, (u8)count);
    outb(ATA_LBA0, (u8)(lba & 0xFF));
    outb(ATA_LBA1, (u8)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (u8)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_READ_SECTORS);

    for (u32 s = 0; s < count; s++) {
        if (wait_clear_bsy() != 0) return -1;
        if (wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++)
            *out++ = inw(ATA_DATA);
    }
    return 0;
}

static int ata_write_impl(DeviceObject *dev, u64 lba, u32 count, const void *buf) {
    u32 drive = *(u32 *)dev->ext;
    const u16 *in = (const u16 *)buf;

    if (wait_clear_bsy() != 0) return -1;
    outb(ATA_DRIVE, (u8)(0xE0 | ((drive & 1) << 4) | ((lba >> 24) & 0x0F)));
    outb(ATA_SECCNT, (u8)count);
    outb(ATA_LBA0, (u8)(lba & 0xFF));
    outb(ATA_LBA1, (u8)((lba >> 8) & 0xFF));
    outb(ATA_LBA2, (u8)((lba >> 16) & 0xFF));
    outb(ATA_CMD, CMD_WRITE_SECTORS);

    for (u32 s = 0; s < count; s++) {
        if (wait_clear_bsy() != 0) return -1;
        if (wait_drq() != 0) return -1;
        for (int i = 0; i < 256; i++)
            outw(ATA_DATA, *in++);
    }
    outb(ATA_CMD, CMD_FLUSH_CACHE);
    wait_clear_bsy();
    return 0;
}

static DriverObject g_ata_driver = { "atapio", ata_read_impl, ata_write_impl };

DeviceObject *ata_register(u32 drive) {
    DeviceObject *dev = IoCreateDevice(&g_ata_driver, "HardDisk", sizeof(u32));
    *(u32 *)dev->ext = drive;

    /* probe: a real drive returns a non-0xFF, non-busy status */
    u8 s = inb(ATA_STATUS);
    if (s == 0xFF) return NULL;
    return dev;
}
