/* Debian95 — I/O manager (Io). NT-flavored driver/device model: drivers register
 * device objects (in the \Device namespace); reads dispatch through the driver. */
#ifndef DEBIAN95_IO_H
#define DEBIAN95_IO_H

#include <debian95/types.h>

typedef struct DeviceObject DeviceObject;
typedef struct DriverObject DriverObject;

typedef int (*DriverReadProc)(DeviceObject *dev, u64 lba, u32 count, void *buf);
typedef int (*DriverWriteProc)(DeviceObject *dev, u64 lba, u32 count, const void *buf);

struct DriverObject {
    const char     *name;
    DriverReadProc  read;      /* returns 0 on success */
    DriverWriteProc write;
};

struct DeviceObject {
    DriverObject *driver;
    void         *ext;        /* device extension (driver-private) */
    u32           sector_size;
};

void          IoInitialize(void);
DeviceObject *IoCreateDevice(DriverObject *drv, const char *name, usize ext_size);
int           IoReadDevice(DeviceObject *dev, u64 lba, u32 count, void *buf);
int           IoWriteDevice(DeviceObject *dev, u64 lba, u32 count, const void *buf);

/* ATA PIO disk driver */
DeviceObject *ata_register(u32 drive);   /* drive: 0 = primary master, 1 = primary slave */

#endif /* DEBIAN95_IO_H */
