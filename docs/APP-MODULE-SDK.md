# Debian95 App Module SDK

App modules let you add or replace a Debian95 application by dropping a shared
object (`.so`) into a directory — no rebuilding or growing the desktop shell.
This is the mechanism the built-in apps are migrating onto.

## How it works

At startup the shell scans the plugins directory, `dlopen()`s every `*.so`,
reads its exported `debian95_module` descriptor, and registers it exactly like a
built-in app. The Start menu / desktop fire a command **id**; `app_launch(id)`
calls the module's `open()`.

- **Default directory:** `/usr/lib/debian95/apps`
- **Override (dev):** set `DEBIAN95_PLUGINS=/path/to/dir`

Modules load into the shell's **own process**, so a module calls the host API
directly — there is no IPC and no launch latency. You get *build* and
*distribution* modularity (hot-swap, the shell core never grows), **not fault
isolation**: a module crash is a shell crash. Apps that need real isolation
(e.g. VLC) stay separate processes framed by `debian95-wm`.

## Writing a module

Copy [`sdk/examples/hello.c`](../sdk/examples/hello.c). A module is one `.c`
that:

1. `#include <debian95/appsdk.h>`
2. implements a window procedure and an `open()` that calls `CreateAppWindow`
3. exports exactly one descriptor:

```c
const Debian95Module debian95_module = {
    DEBIAN95_MODULE_ABI,   /* ABI version — the loader refuses a mismatch       */
    200,                  /* command id (unique; >= 100 for third-party apps)  */
    "My App",             /* display name                                      */
    IC_APP,               /* icon id (debian95/icons.h), or 0                   */
    0,                    /* icon module e.g. "MSPAINT.EXE", or 0              */
    myapp_open,           /* launch                                            */
    3                     /* Start>Programs group, or -1 (see below)           */
};
```

**Where it appears.** `category` 0–3 (Internet / Multimedia / Graphics / Office)
puts the app in `Start > Programs > <group>`. Use `-1` only when the app already
has a static menu entry that fires its id (that is how the migrated built-ins
keep their original Accessories/Games placement).

**Opening on a document (ABI 2).** A module may add an eighth descriptor field,
`open_doc(const char *path)`, so the shell can launch it on a file (double-click
a `.txt`/`.rtf`/`.wav`, etc.). File associations call `app_launch_doc(id, path)`,
which invokes `open_doc` if present and otherwise falls back to `open()`:

```c
static void myapp_open(void)                 { myapp_doc(0); }      /* blank/new */
const Debian95Module debian95_module = {
    DEBIAN95_MODULE_ABI, 120, "My App", IC_DOC, 0, myapp_open, 3, myapp_doc
};
```

## Building and installing

```bash
cc -fPIC -shared -DDEBIAN95_MODULE -I<repo>/sdk/include myapp.c -o myapp.so
cp myapp.so /usr/lib/debian95/apps/        # appears on next desktop start
rm /usr/lib/debian95/apps/myapp.so         # and it's gone
```

Do **not** link any of the shell's object files; the host API resolves at load
time from the desktop binary (which is linked `-rdynamic`). Link only what your
own code needs (e.g. `-lm`).

Built-in modules are built by the image pipeline: add the app's name to
`PLUGINS` in `usermode/desktop/Makefile`; `tools/build-debian95-desktop.sh`
compiles them and installs them to `/usr/lib/debian95/apps`.

## The stable ABI

The ABI is the host-API surface declared by the headers `appsdk.h` pulls in:

| Header | What a module may call |
|---|---|
| `win32.h` | `CreateAppWindow`, `DestroyWindow`, `SetWindowSize`, `SetMenuW32`, `BeginPaint`/`EndPaint`, `TextOut`, `FillRect`, `SetTextColor`, `DrawEdge3D`, `MessageBoxW32`, `DefWindowProc`, `InvalidateRect`, dialogs/controls … |
| `wm.h` | `wm_clipboard_get/set`, `wm_bevel_raised/sunken`, `wm_register_program_cat` … |
| `fb.h` | `fb_pixel`, `fb_fill_rect`, `fb_rect`, `fb_text_t` … |
| `font.h` | `font_height`, `font_measure` |
| `icons.h` | `IC_*` icon ids |

Treat these as the contract. `DEBIAN95_MODULE_ABI` is bumped on any
source-incompatible change to the descriptor or this surface, and the loader
skips (with a log line) any module whose `abi` field does not match — so an old
`.so` fails safe instead of crashing the shell.

## Porting a built-in to a module

1. Confirm **no other compiled file uses the app's symbols** (otherwise removing
   it from the shell breaks the link — those apps stay built-in for now).
2. Append the descriptor to the app's `.c`, guarded by `#ifdef DEBIAN95_MODULE`,
   so the source is unambiguous about being a module.
3. Remove the app from `WIN32` and from `app_register_builtin()`; add its name to
   `PLUGINS`. Its static Start-menu entry keeps firing the same id.
4. Build and boot; confirm the loader logs it and the window still works.
