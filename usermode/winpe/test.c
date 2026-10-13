/* Debian95 — a Windows PE test program. Built with a Windows toolchain
 * (clang x86_64-pc-windows + lld-link) into a real PE32+ .exe, imports the
 * kernel32 functions Debian95's PE loader provides. Original code. */
__declspec(dllimport) void *GetStdHandle(unsigned long);
__declspec(dllimport) int   WriteFile(void *, const void *, unsigned long, unsigned long *, void *);
__declspec(dllimport) void  ExitProcess(unsigned int);
__declspec(dllimport) int   MessageBoxA(void *, const char *, const char *, unsigned int);

static unsigned slen(const char *s) { unsigned n = 0; while (s[n]) n++; return n; }

void pe_start(void) {
    void *out = GetStdHandle((unsigned long)-11);   /* STD_OUTPUT_HANDLE */
    const char *msg = "  [PE] Hello from a real Windows PE .exe running on Debian95!\n";
    unsigned long written;
    WriteFile(out, msg, slen(msg), &written, 0);
    MessageBoxA(0, "Hello from a Windows .exe,\nrunning on Debian95!", "TEST.EXE", 0);
    ExitProcess(0);
}
