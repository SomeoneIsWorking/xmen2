#include "x2run_diag.h"
#include "x86rt.h"
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef X86_WATCH
FILE *x86_watch_log(void);
void x86_watch_note_dump(FILE *o);
static DWORD WINAPI live_boundary_watch(LPVOID unused)
{
    FILE *o = x86_watch_log();
    const char *text = getenv("X2_WATCH_LIVE_MS");
    DWORD delay = text && *text ? (DWORD)strtoul(text, NULL, 0) : 250u;
    unsigned long snapshot = 0;
    (void)unused;
    if (delay < 10u || delay > 10000u) {
        fprintf(o, "[LIVE] X2_WATCH_LIVE_MS=%lu is outside 10..10000; refusing\n",
                (unsigned long)delay);
        fflush(o);
        return 0;
    }
    for (;;) {
        Sleep(delay);
        fprintf(o, "[LIVE %lu] boundary snapshot\n", ++snapshot);
        x86_watch_note_dump(o);
        fflush(o);
    }
}
#endif

void x2run_diag_note(const char *what, uint32_t value)
{
    FILE *o = fopen("x2run-state.log", "a");
    if (!o) return;
    fprintf(o, "%s 0x%08x\n", what, value);
    fclose(o);
}

void x86_runtime_fault_note(const char *kind, uint32_t a, uint32_t b, uint32_t c)
{
    FILE *o = fopen("x86-runtime-fault.log", "w");
    if (!o) return;
    fprintf(o, "%s a=0x%08x b=0x%08x c=0x%08x\n", kind, a, b, c);
    fclose(o);
}

static void note_exit(void) { x2run_diag_note("runner atexit", 0); }

void x2run_diag_start(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    remove("x2run-state.log");
    atexit(note_exit);
#ifdef X86_WATCH
    x86_watch_selftest();
    x86_fault_install();
    if (getenv("X2_WATCH_LIVE"))
        CreateThread(NULL, 0, live_boundary_watch, NULL, 0, NULL);
#endif
}
