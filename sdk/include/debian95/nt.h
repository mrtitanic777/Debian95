/* Debian95 — Native API. Kernel-side implementations exposed to user mode through
 * the syscall gate; a File is an Ob object holding a FAT file's contents. */
#ifndef DEBIAN95_NT_H
#define DEBIAN95_NT_H

#include <debian95/types.h>
#include <debian95/io.h>

#define STATUS_SUCCESS        0
#define STATUS_NOT_FOUND    (-1)
#define STATUS_INVALID      (-2)

/* Mount the boot filesystem on `disk` and register the File object type. */
void FsInitialize(DeviceObject *disk);

/* Open a file by name (root of the mounted volume); writes a handle to *out. */
i64 NtCreateFile(const char *name, u32 *out_handle);

/* Read up to `len` bytes from the file at its current position. */
i64 NtReadFile(u32 handle, void *buf, u32 len, u32 *bytes_read);

/* Close a handle. */
i64 NtClose(u32 handle);

/* List the mounted volume's root directory. Returns entry count. */
int FsList(char names[][16], u32 *sizes, int max);

/* A directory entry (used by FsListDir for a real, explorable tree). */
typedef struct { char name[16]; u32 size; int is_dir; u32 mtime; } FsEntry;   /* mtime = unix seconds */
/* List a subdirectory relative to the volume root ("" = root). Directories are
 * reported with is_dir = 1. Returns the entry count. */
int FsListDir(const char *path, FsEntry *entries, int max);
/* Read/write/delete accept a path relative to the root (subdirs with '/'). */

/* Read a file by name into a freshly allocated buffer. 0 on success. */
int FsReadAlloc(const char *name, u8 **out, u32 *size);

/* Create/overwrite a file with the given data. 0 on success. */
int FsWriteFile(const char *name, const u8 *data, u32 size);
int FsDelete(const char *name);                 /* delete a file; 0 on success */
int FsRmdir(const char *path);                  /* recursively delete a directory; 0 on success */
int FsMkdir(const char *path);                   /* create a directory; 0 on success */
int FsRename(const char *oldpath, const char *newpath);  /* rename within the volume; 0 on success */
i64 NtWriteFile(const char *name, const void *buf, u32 len);

#endif /* DEBIAN95_NT_H */
