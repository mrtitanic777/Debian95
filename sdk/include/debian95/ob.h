/* Debian95 — Object Manager (Ob). NT-style typed objects with reference
 * counting, a '\'-rooted namespace, and a handle table. Every higher-level
 * kernel abstraction (processes, files, events, the registry) is an object. */
#ifndef DEBIAN95_OB_H
#define DEBIAN95_OB_H

#include <debian95/types.h>

typedef struct ObType   ObType;      /* opaque object type descriptor */
typedef void (*ObDeleteProc)(void *body);

void ObInitialize(void);

/* Register a new object type. `del` (optional) runs when the last reference
 * to an object of this type is released. */
ObType *ObCreateType(const char *name, ObDeleteProc del);

/* Create an object. If `parent_dir` (a directory body, or NULL) and `name` are
 * given, it is inserted into the namespace. Returns the object BODY; created
 * with one reference. */
void *ObCreateObject(ObType *type, void *parent_dir, const char *name, usize body_size);

/* Create a namespace directory under `parent_dir` (NULL for a detached dir). */
void *ObCreateDirectory(void *parent_dir, const char *name);

/* The root directory body (i.e. "\"). */
void *ObRoot(void);

/* Iterate a directory's children, calling visit(name, body, ctx) for each. */
void ObListDir(void *dir_body, void (*visit)(const char *name, void *body, void *ctx), void *ctx);

/* Resolve a '\'-rooted path (e.g. "\\Device\\Timer"); adds a reference. */
void *ObLookup(const char *path);

/* Pointer references. */
void ObReferenceObject(void *body);
void ObDereferenceObject(void *body);
i64  ObGetRefCount(void *body);

/* Handle tables. Ops act on the current table (set per process by Ps). */
typedef struct ObHandleTable ObHandleTable;
ObHandleTable *ObCreateHandleTable(void);
void           ObSetCurrentHandleTable(ObHandleTable *t);
ObHandleTable *ObGetCurrentHandleTable(void);

u32   ObOpenHandle(void *body);              /* 0 = failure */
void *ObReferenceObjectByHandle(u32 handle); /* adds a reference; NULL if bad */
void  ObCloseHandle(u32 handle);

#endif /* DEBIAN95_OB_H */
