/*
 * Crash reporter for the recompiled PC modules.
 *
 * The entry-point watch (src/x86watch.c) answers "which recompiled function
 * ran, and what did it see". It cannot answer the question that actually
 * blocks this bisection: when a recompiled path dies at an EIP that is in no
 * module, WHO transferred control there. The watch's last line is the function
 * that was entered and never returned -- everything between that and the fault
 * is invisible to it, because it is host code.
 *
 * A debugger would answer it, but this game is a GUI-subsystem process under
 * `wine explorer /desktop=`: winedbg attached that way produces no output at
 * all (measured -- scratch/logs/ark-dbg.log is four lines of Wine banner). So
 * the reporter has to live inside the process and write to the same log the
 * watch does.
 *
 *   X2_FAULT=0          disable (default: installed in every X86_WATCH build)
 *   X2_FAULT_SELFTEST=1 prove the handler fires before the game runs
 *   X2_FAULT_STACK=64   stack slots to dump (default 64)
 *
 * Design rule for this file: it must be able to report the NEGATIVE. If no
 * exception is ever seen, the exit report SAYS so, in those words -- otherwise
 * a log with no [FAULT] block is indistinguishable from a handler that was
 * never installed, and "the process died silently" would read as evidence.
 */
#include "x86rt.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The watch owns the log file; sharing it keeps the fault block in sequence
   with the ENTER lines that lead up to it, which is the whole point. */
FILE *x86_watch_log(void);
void x86_watch_note_dump(FILE *o);

#define SELFTEST_CODE 0xE0F00001u   /* private range: nothing else raises it */

static PVOID         f_veh;
static volatile LONG f_seen;         /* fatal exceptions reported */
static volatile LONG f_other;        /* non-fatal first-chance exceptions */
static volatile LONG f_selftest_hit;
static int           f_stack_slots = 64;

/* ---- safe memory access ------------------------------------------------ */

static int mem_readable(const void *p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    const unsigned char *a = (const unsigned char *)p;
    const unsigned char *end = a + n;
    while (a < end) {
        if (!VirtualQuery(a, &mbi, sizeof mbi)) return 0;
        if (mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        a = (const unsigned char *)mbi.BaseAddress + mbi.RegionSize;
    }
    return 1;
}

/*
 * Name the region an address falls in.
 *
 * "in no module" is the finding that matters here, so it is spelled out with
 * the region's state and protection rather than left as an empty string: an
 * executable heap block and a freed reservation are very different stories and
 * both would otherwise print as nothing.
 */
static void describe(uint32_t addr, char *buf, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    char path[MAX_PATH];
    HMODULE m;

    if (!addr) { snprintf(buf, n, "NULL"); return; }
    if (!VirtualQuery((void *)(uintptr_t)addr, &mbi, sizeof mbi)) {
        snprintf(buf, n, "unmapped");
        return;
    }
    if (mbi.State != MEM_COMMIT) {
        snprintf(buf, n, "not committed (state=0x%lx)", (unsigned long)mbi.State);
        return;
    }
    m = (HMODULE)mbi.AllocationBase;
    if (m && GetModuleFileNameA(m, path, sizeof path)) {
        const char *b = strrchr(path, '\\');
        b = b ? b + 1 : path;
        snprintf(buf, n, "%s+0x%x", b, (unsigned)(addr - (uint32_t)(uintptr_t)m));
        return;
    }
    snprintf(buf, n, "NOT IN ANY MODULE (base=%p prot=0x%lx%s)",
             mbi.AllocationBase, (unsigned long)mbi.Protect,
             (mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ
                             | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
                 ? " EXECUTABLE" : "");
}

/* Guest addresses are the ones the disassembly and the bisect output use, so
   print them too whenever a host address falls inside the original image. */
static void guest_note(uint32_t addr, char *buf, size_t n)
{
    uint32_t base = G_IMGBASE;
    if (base && addr >= base && addr - base < 0x400000u)
        snprintf(buf, n, "  [guest 0x%08x]", 0x10000000u + (addr - base));
    else
        buf[0] = '\0';
}

/* ---- the report -------------------------------------------------------- */

static void dump_regs(FILE *o, const CONTEXT *c)
{
    fprintf(o, "[FAULT]   eip=%08lx esp=%08lx ebp=%08lx eflags=%08lx\n",
            (unsigned long)c->Eip, (unsigned long)c->Esp,
            (unsigned long)c->Ebp, (unsigned long)c->EFlags);
    fprintf(o, "[FAULT]   eax=%08lx ecx=%08lx edx=%08lx ebx=%08lx\n",
            (unsigned long)c->Eax, (unsigned long)c->Ecx,
            (unsigned long)c->Edx, (unsigned long)c->Ebx);
    fprintf(o, "[FAULT]   esi=%08lx edi=%08lx\n",
            (unsigned long)c->Esi, (unsigned long)c->Edi);
}

static void dump_code(FILE *o, uint32_t eip)
{
    int i;
    if (!mem_readable((void *)(uintptr_t)eip, 24)) {
        fprintf(o, "[FAULT]   code at eip: UNREADABLE -- control reached an "
                   "address with no committed page\n");
        return;
    }
    fprintf(o, "[FAULT]   code at eip:");
    for (i = 0; i < 24; i++)
        fprintf(o, " %02x", *(const unsigned char *)(uintptr_t)(eip + i));
    fprintf(o, "\n");
}

/*
 * Is `v` plausibly a RETURN address -- i.e. do the bytes just before it decode
 * as a call?
 *
 * Module attribution alone is not enough to read a call chain: a stack full of
 * pointers into three modules has no ordering, and picking the ones that look
 * like frames by eye is how a wrong caller gets attributed. x86 calls are
 * 2, 3, 5, 6 or 7 bytes, so checking each of those distances for a call opcode
 * turns "points into libIGCore" into "was pushed by a call at libIGCore+X",
 * which names the call SITE.
 *
 * It is a heuristic and says so in the output: `E8`/`FF /2` bytes occur inside
 * other instructions, so a false positive is possible. It cannot produce a
 * false NEGATIVE for a real call, which is the direction that would mislead.
 */
static int call_site_before(uint32_t v, uint32_t *site)
{
    static const int len[] = { 2, 3, 5, 6, 7 };
    unsigned i;
    for (i = 0; i < sizeof len / sizeof len[0]; i++) {
        uint32_t s = v - (uint32_t)len[i];
        const unsigned char *p = (const unsigned char *)(uintptr_t)s;
        if (!mem_readable(p, (size_t)len[i])) continue;
        if (len[i] == 5 && p[0] == 0xE8) { *site = s; return 1; }   /* call rel32 */
        if (p[0] == 0xFF && ((p[1] >> 3) & 7) == 2) { *site = s; return 1; }
        if (p[0] == 0x9A && len[i] == 7) { *site = s; return 1; }   /* call far */
    }
    return 0;
}

/*
 * The stack, annotated.
 *
 * This is the part that answers "who called this". A raw hex dump would not:
 * the one fact worth having is which slots point into which module, because a
 * return address into libIGCore is the caller and a return address into the
 * recompiled DLL is our own code.
 */
static void dump_stack(FILE *o, uint32_t esp)
{
    int i;
    if (!mem_readable((void *)(uintptr_t)esp, 4)) {
        fprintf(o, "[FAULT]   stack at esp: UNREADABLE (esp=%08x) -- the stack "
                   "pointer itself is bad, so there is no call chain to read\n",
                esp);
        return;
    }
    fprintf(o, "[FAULT]   stack (%d slots from esp), module-annotated:\n",
            f_stack_slots);
    for (i = 0; i < f_stack_slots; i++) {
        uint32_t a = esp + (uint32_t)(i * 4), v;
        char what[160], g[64];
        if (!mem_readable((void *)(uintptr_t)a, 4)) {
            fprintf(o, "[FAULT]     %08x: <unreadable, stack dump stops here>\n", a);
            return;
        }
        v = *(const uint32_t *)(uintptr_t)a;
        describe(v, what, sizeof what);
        guest_note(v, g, sizeof g);
        {
            uint32_t site = 0;
            char site_s[160], site_g[64];
            if (call_site_before(v, &site)) {
                describe(site, site_s, sizeof site_s);
                guest_note(site, site_g, sizeof site_g);
                fprintf(o, "[FAULT]     %08x: %08x  %s%s  <- RET from call at "
                           "%08x %s%s\n",
                        a, v, what, g, site, site_s, site_g);
            } else {
                fprintf(o, "[FAULT]     %08x: %08x  %s%s\n", a, v, what, g);
            }
        }
    }
}

static int fatal_code(DWORD c)
{
    return c == EXCEPTION_ACCESS_VIOLATION
        || c == EXCEPTION_ILLEGAL_INSTRUCTION
        || c == EXCEPTION_PRIV_INSTRUCTION
        || c == EXCEPTION_IN_PAGE_ERROR
        || c == EXCEPTION_STACK_OVERFLOW
        || c == EXCEPTION_ARRAY_BOUNDS_EXCEEDED
        || c == EXCEPTION_INT_DIVIDE_BY_ZERO
        /* An INT3 with no debugger attached is a crash here, not a stop: it
           means control reached a 0xCC byte, which is what padding and freed
           stack look like. Leaving it out of this list once cost a run whose
           only symptom was Wine's one-line "unhandled exception". */
        || c == EXCEPTION_BREAKPOINT
        || c == EXCEPTION_ILLEGAL_INSTRUCTION;
}

static LONG CALLBACK fault_veh(EXCEPTION_POINTERS *ep)
{
    const EXCEPTION_RECORD *er = ep->ExceptionRecord;
    FILE *o = x86_watch_log();
    char what[160], g[64];

    if (er->ExceptionCode == SELFTEST_CODE) {
        f_selftest_hit = 1;
        fprintf(o, "[FAULT] SELFTEST: the handler saw a deliberately raised "
                   "exception, so a real fault will be reported too.\n");
        fflush(o);
        return EXCEPTION_CONTINUE_EXECUTION;   /* resume after RaiseException */
    }
    if (!fatal_code(er->ExceptionCode)) {
        /* C++ throws and debugger notifications are normal here; counting them
           rather than printing keeps the log readable but still says the
           handler was live. */
        f_other++;
        return EXCEPTION_CONTINUE_SEARCH;
    }
    /* Only the first: after the first fault the process state is not worth
       another 64-line dump, and a fault loop would fill the disk. */
    if (InterlockedIncrement(&f_seen) != 1) return EXCEPTION_CONTINUE_SEARCH;

    fprintf(o, "[FAULT] ================ fault ================\n");
    fprintf(o, "[FAULT]   code=0x%08lx at %p (thread %lu)\n",
            (unsigned long)er->ExceptionCode, er->ExceptionAddress,
            (unsigned long)GetCurrentThreadId());
    describe((uint32_t)(uintptr_t)er->ExceptionAddress, what, sizeof what);
    guest_note((uint32_t)(uintptr_t)er->ExceptionAddress, g, sizeof g);
    fprintf(o, "[FAULT]   fault address is in: %s%s\n", what, g);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        && er->NumberParameters >= 2) {
        uint32_t tgt = (uint32_t)er->ExceptionInformation[1];
        describe(tgt, what, sizeof what);
        fprintf(o, "[FAULT]   %s access to %08x, which is in: %s\n",
                er->ExceptionInformation[0] == 1 ? "write"
                    : er->ExceptionInformation[0] == 8 ? "execute" : "read",
                tgt, what);
    }
    fprintf(o, "[FAULT]   g_imgbase=%08x (original image; guest 0x10000000)\n",
            G_IMGBASE);
    x86_watch_note_dump(o);
    dump_regs(o, ep->ContextRecord);
    dump_code(o, (uint32_t)ep->ContextRecord->Eip);
    dump_stack(o, (uint32_t)ep->ContextRecord->Esp);
    fprintf(o, "[FAULT] =======================================\n");
    fflush(o);
    return EXCEPTION_CONTINUE_SEARCH;          /* let it die as it would have */
}

static void fault_report(void)
{
    FILE *o = x86_watch_log();
    if (f_seen)
        fprintf(o, "[FAULT] %ld fatal exception(s) this run (reported the first)"
                   "; %ld non-fatal first-chance exception(s) passed through\n",
                (long)f_seen, (long)f_other);
    else
        fprintf(o, "[FAULT] no fatal exception reached the handler this run "
                   "(%ld non-fatal first-chance exception(s) did, so the "
                   "handler was live). If the process died anyway, it did not "
                   "die on a CPU exception in this process.\n", (long)f_other);
    fflush(o);
}

void x86_fault_install(void)
{
    const char *e = getenv("X2_FAULT");
    const char *n = getenv("X2_FAULT_STACK");
    FILE *o;
    if (e && e[0] == '0') return;
    if (n && *n) {
        int v = atoi(n);
        if (v > 0 && v <= 512) f_stack_slots = v;
    }
    o = x86_watch_log();
    /* FIRST in the chain, so a fault is reported before anything else in the
       process gets a chance to swallow it. */
    f_veh = AddVectoredExceptionHandler(1, fault_veh);
    if (!f_veh) {
        fprintf(o, "[FAULT] AddVectoredExceptionHandler FAILED (%lu) -- there "
                   "will be NO fault report this run\n",
                (unsigned long)GetLastError());
        fflush(o);
        return;
    }
    fprintf(o, "[FAULT] handler installed, %d stack slots\n", f_stack_slots);
    fflush(o);
    atexit(fault_report);

    e = getenv("X2_FAULT_SELFTEST");
    if (e && e[0] == '1') {
        RaiseException(SELFTEST_CODE, 0, 0, NULL);
        if (!f_selftest_hit) {
            fprintf(o, "[FAULT] SELFTEST FAILED: a deliberately raised exception "
                       "did not reach the handler. Absence of a [FAULT] block "
                       "this run means NOTHING.\n");
            fflush(o);
        }
    }
}
