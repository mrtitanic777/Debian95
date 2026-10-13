/* Debian95 — shared shell formatting helpers (Explorer / Find / Recycle Bin). */
#ifndef DEBIAN95_SHELLUTIL_H
#define DEBIAN95_SHELLUTIL_H
#include <debian95/types.h>

/* file size as Win95 shows it: KB (rounded up, min 1), scaling to MB/GB/TB. */
void shell_size(u32 bytes, char *out);
/* unix-seconds -> "M/D/YY h:mm AM/PM" (blank if epoch is 0). */
void shell_date(u32 epoch, char *out);
/* a friendly type name from a file name / dir flag, e.g. "Text Document". */
const char *shell_type(const char *name, int is_dir);

#endif
