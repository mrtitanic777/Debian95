/* Debian95 — app module loader. Scans a directory for *.so plugins, dlopen()s
 * each, validates its `debian95_module` descriptor, and registers it so the
 * Start menu / desktop can launch it exactly like a built-in app. See
 * sdk/include/debian95/appsdk.h for the module contract. */
#include <debian95/appsdk.h>
#include <debian95/appreg.h>
#include <dlfcn.h>
#include <dirent.h>
#include <string.h>
#include <stdio.h>

int modules_load(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) return 0;                                /* no plugins dir -> nothing to load */
    struct dirent *e; int n = 0; char path[1024];
    while ((e = readdir(d)) != 0) {
        const char *nm = e->d_name; size_t L = strlen(nm);
        if (L < 4 || strcmp(nm + L - 3, ".so") != 0) continue;
        snprintf(path, sizeof path, "%s/%s", dir, nm);
        /* RTLD_GLOBAL so a module's own later-resolved deps can see it; the host
         * API symbols the module needs come from the -rdynamic main binary. */
        void *h = dlopen(path, RTLD_NOW | RTLD_GLOBAL);
        if (!h) { fprintf(stderr, "module %s: %s\n", nm, dlerror()); continue; }
        const Debian95Module *m = (const Debian95Module *)dlsym(h, "debian95_module");
        if (!m) { fprintf(stderr, "module %s: no debian95_module descriptor\n", nm); dlclose(h); continue; }
        if (m->abi != DEBIAN95_MODULE_ABI) {
            fprintf(stderr, "module %s: ABI %d, host wants %d - skipped\n", nm, m->abi, DEBIAN95_MODULE_ABI);
            dlclose(h); continue;
        }
        if (!m->open || !m->name) { fprintf(stderr, "module %s: incomplete descriptor\n", nm); dlclose(h); continue; }
        app_register(m->id, m->name, m->icon, m->imod, m->open);
        if (m->open_doc) app_register_doc(m->id, m->open_doc);       /* file-association opener */
        if (m->category >= 0) wm_register_program_cat(m->name, m->id, 0, 1, m->category);
        fprintf(stderr, "module loaded: %s -> \"%s\" (id %d)\n", nm, m->name, m->id);
        n++;
    }
    closedir(d);
    return n;
}
