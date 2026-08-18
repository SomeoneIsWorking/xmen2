/*
 * x2native -- the recompiled game code running as a NATIVE Linux binary.
 *
 * No Wine, no PE loader, no original DLL alongside us. This is the first
 * artefact on the rc-native track, and its job is to make that track
 * measurable rather than aspirational: it maps the original image at its own
 * base, runs recompiled function bodies against it, and opens an SDL window so
 * the platform layer that replaces USER32/DINPUT/D3D8 has somewhere to live.
 *
 *   ./x2native                         run the current SDL3 GPU game target
 *   ./x2native --no-window             the same target, off-screen
 *
 * It is a hybrid port: mechanically translated game/engine bodies stay live
 * while focused native modules replace platform and engine boundaries. Any
 * still-unimplemented host import aborts by name, so incomplete coverage is a
 * concrete stop rather than a silently skipped operation.
 */
#include "pe_map.h"
#include "control.h"
#include "guest_clock.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "heartbeat.h"
#include "dinput_device.h"
#include "win32_sdl.h"
#include "gpu_device.h"
#include "shell32.h"
#include "advapi32.h"
#include "threads.h"
#include "guest_heap.h"
#include "d3d8_host.h"
#include "../d3d8/d3d8_drawcall.h"
#include "d3d8_com.h"
#include "env_file.h"
#include "x2native_options.h"

#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

__thread uint32_t g_fsbase, g_gsbase;

/* argv[0], so the fault reporter can print an addr2line command that is
   runnable as printed rather than one the reader has to complete. */
static const char *g_argv0;

/* ---- import binding ----------------------------------------------------
 *
 * A loader's job, and nobody else was doing it. Calls reach their target
 * through the named imp_* stubs, but 40 of libIGDisplay's IAT slots are READ
 * AS DATA in 113 places -- ArkCore, kSuccess, the memory-pool adaptor -- and in
 * a file image those slots still hold hint/name RVAs. Unbound, every one of
 * those reads returns a small integer that looks like a pointer and nothing
 * complains.
 *
 * Unresolvable slots get a distinct address in a PROT_NONE page rather than 0,
 * so using one faults immediately at an address that identifies WHICH import
 * -- instead of reading as NULL and taking a plausible early-out.
 */
#define POISON_BASE 0x00090000u
#define POISON_SIZE 0x00010000u
#define POISON_STRIDE 16u

static struct { uint32_t addr; const char *mod; char sym[192]; } g_poison[512];
static int g_npoison, g_nbound;

static uint32_t poison_for(const char *mod, const char *sym, uint32_t ordinal)
{
    uint32_t a;
    if (g_npoison == (int)(sizeof g_poison / sizeof g_poison[0]))
        return POISON_BASE;                  /* out of slots: share the first */
    a = POISON_BASE + (uint32_t)g_npoison * POISON_STRIDE;
    g_poison[g_npoison].addr = a;
    g_poison[g_npoison].mod = mod;
    if (sym) snprintf(g_poison[g_npoison].sym, sizeof g_poison[g_npoison].sym,
                      "%s", sym);
    else snprintf(g_poison[g_npoison].sym, sizeof g_poison[g_npoison].sym,
                  "#%u (by ordinal)", ordinal);
    g_npoison++;
    return a;
}

const char *x86_poison_name(uint32_t addr, const char **mod);
const char *x86_thunk_name(uint32_t addr, const char **mod);

static const char *poison_name(uint32_t addr, const char **mod)
{
    int i;
    for (i = 0; i < g_npoison; i++)
        if (addr >= g_poison[i].addr && addr < g_poison[i].addr + POISON_STRIDE) {
            *mod = g_poison[i].mod;
            return g_poison[i].sym;
        }
    return NULL;
}

uint32_t x86_native_data_export(const char *mod, const char *sym);
void x86_native_data_arena(uint32_t base, uint32_t size);

#define DATA_ARENA 0x000B0000u
#define DATA_SIZE  0x1000u

static uint32_t resolve_import(const char *mod, const char *sym,
                               int by_ordinal, uint32_t ordinal, void *ctx)
{
    X86Module *m;
    (void)ctx;
    if (!by_ordinal && sym) {
        /* A DATA export the native layer supplies: it must be a real word in
           guest memory, because the guest reads it through the slot. */
        uint32_t d = x86_native_data_export(mod, sym);
        if (d) { g_nbound++; return d; }
        for (m = x86_modules(); m; m = m->next) {
            uint32_t rva;
            if (strcasecmp(m->name, mod) != 0) continue;
            rva = pe_export_rva(*m->base, sym);
            if (rva) { g_nbound++; return *m->base + rva; }
            break;                            /* right module, no such export */
        }
    }
    /* Not another recompiled module. If some module implements it natively,
       bind the slot to a thunk so a call THROUGH the slot works the same as a
       call to the named stub. */
    if (!by_ordinal && sym) {
        uint32_t t = x86_native_thunk(mod, sym);
        if (t) { g_nbound++; return t; }
    }
    return poison_for(mod, sym, ordinal);
}

/*
 * What a fatal signal MEANS, in the words its si_code carries.
 *
 * Reported because "the process died" was, for every signal except SIGSEGV,
 * the entire report: only SIGSEGV was handled, so an illegal instruction, a
 * misaligned access or a divide by zero killed the run with NOTHING printed --
 * indistinguishable, from outside, from the window simply closing. That is the
 * shape of the crash this reporter was extended to name.
 */
static const char *fault_meaning(int sig, int code)
{
    switch (sig) {
    case SIGSEGV:
        return code == SEGV_MAPERR ? "address not mapped"
             : code == SEGV_ACCERR ? "no permission for that access"
             : "a memory access fault";
    case SIGILL:
        return code == ILL_ILLOPC ? "illegal OPCODE -- the instruction at this "
                                    "address is not an instruction"
             : code == ILL_ILLOPN ? "illegal operand"
             : code == ILL_ILLADR ? "illegal addressing mode"
             : code == ILL_PRVOPC ? "privileged opcode"
             : code == ILL_ILLTRP ? "illegal trap"
             : "an illegal instruction";
    case SIGFPE:
        return code == FPE_INTDIV ? "integer divide by zero"
             : code == FPE_INTOVF ? "integer overflow"
             : code == FPE_FLTDIV ? "floating-point divide by zero"
             : code == FPE_FLTINV ? "invalid floating-point operation"
             : "an arithmetic fault";
    case SIGBUS:
        return code == BUS_ADRALN ? "misaligned address"
             : code == BUS_ADRERR ? "no such physical address"
             : code == BUS_OBJERR ? "object-specific hardware error"
             : "a bus error";
    case SIGTRAP:
        return "a trap instruction (INT3/INT1) executed with no debugger to "
               "take it";
    default:
        return "a fatal signal";
    }
}

static const char *fault_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGILL:  return "SIGILL";
    case SIGFPE:  return "SIGFPE";
    case SIGBUS:  return "SIGBUS";
    case SIGTRAP: return "SIGTRAP";
    default:      return "signal";
    }
}

/*
 * A fault in the poison region is an unbound import being used. Say which.
 *
 * Every other fatal signal lands here too, and the import analysis below is
 * SIGSEGV's alone: for SIGILL/SIGTRAP `si_addr` is the instruction, for SIGFPE
 * the faulting operation, and reading any of them as an import slot would
 * invent an explanation. What they share is the context under `where:` -- the
 * host rip, the guest registers and the boundary ring -- which is the part
 * that names where the guest was.
 */
static void fault_report(int sig, siginfo_t *si, void *uc)
{
    uint32_t a = (uint32_t)(uintptr_t)si->si_addr;
    const char *mod = NULL, *sym;
    (void)uc;
    if (sig != SIGSEGV) {
        fprintf(stderr, "\n*** %s at %p -- %s\n",
                fault_name(sig), si->si_addr, fault_meaning(sig, si->si_code));
        if (sig == SIGILL || sig == SIGTRAP)
            fprintf(stderr,
                "    For a recompiled body this usually means control reached "
                "something that is not code:\n"
                "    a jump through a stale or wrong function pointer, or a "
                "guest RET onto a corrupted stack.\n"
                "    The guest registers and the ring below say where the run "
                "was; si_addr is the host address it tried to execute.\n");
        goto where;
    }
    sym = x86_thunk_name(a, &mod);
    if (sym) {
        /*
         * Report the OBSERVATION, then the two readings of it -- the original
         * message asserted "this import is DATA", which is one conclusion and
         * was the wrong one the first time a class callback landed here. What
         * is actually known is that the synthetic range is unmapped and
         * something touched this address instead of calling it.
         */
        fprintf(stderr, "\n*** the synthetic address 0x%08x was ACCESSED, not "
                        "called: %s!%s\n"
                        "    That range is deliberately unmapped, so any read, "
                        "write or jump into it faults here.\n"
                        "    Either the guest wants this symbol's VALUE (an "
                        "import that is data, not a function -- see\n"
                        "    x86_native_data_export), or a call reached it by a "
                        "path that bypasses the dispatcher.\n",
                a, mod, sym);
        goto where;
    }
    sym = poison_name(a, &mod);
    if (sym) {
        fprintf(stderr, "\n*** unbound import used: %s!%s\n"
                        "    The guest read or called import slot 0x%08x, which "
                        "nothing could resolve.\n"
                        "    That module is either not linked into this binary "
                        "or does not export that symbol.\n", mod, sym, a);
        _exit(3);
    }
    /*
     * Not an import slot, so this is the guest faulting on its own terms and
     * the address alone says nothing -- "SIGSEGV at 0x4" was the entire report
     * for the failure in issue #14, which is indistinguishable from a crash
     * with no context at all. Everything the process still knows goes out
     * here, and what it CANNOT know is stated rather than left as silence.
     */
    fprintf(stderr, "\n*** SIGSEGV at %p (not an import slot) -- %s\n",
            si->si_addr, fault_meaning(sig, si->si_code));
where:
    {
#if defined(__x86_64__) && defined(REG_RIP)
        ucontext_t *u = (ucontext_t *)uc;
        unsigned long rip = (unsigned long)u->uc_mcontext.gregs[REG_RIP];
        Dl_info di;
        int known = dladdr((void *)rip, &di) && di.dli_fbase;
        fprintf(stderr, "    host rip 0x%lx", rip);
        if (known && di.dli_sname) fprintf(stderr, "  in %s", di.dli_sname);
        fputc('\n', stderr);
        /* This binary is PIE, so the runtime rip is NOT a file offset and an
           addr2line on it silently answers "??" -- a command that looks
           runnable and quietly says nothing. Subtract the load base. */
        if (known)
            fprintf(stderr, "    name the generated body with:  addr2line -fCe "
                            "%s 0x%lx\n", g_argv0 ? g_argv0 : "<this binary>",
                    rip - (unsigned long)di.dli_fbase);
        else
            fprintf(stderr, "    (dladdr could not give the load base, so this "
                            "rip cannot be turned into a file offset here)\n");
#else
        (void)uc;
        fprintf(stderr, "    (no host rip: this reporter reads the fault "
                        "context only on x86-64)\n");
#endif
    }
    x86_regs_dump();
    /*
     * The engine's thread list is NOT printed here, and that is a correction
     * rather than an omission.
     *
     * It was called from this handler, because issue #61's fault is exactly
     * the case that wants it and the shutdown report below never runs on this
     * path. But `guest_engine_thread_report` uses printf, and stdio in a
     * signal handler deadlocks on the lock the interrupted code may hold --
     * the same hazard that made the SIGTERM report write(2) its lines (issue
     * #34). What actually happened was worse than a deadlock: the process died
     * of a second SIGSEGV inside the handler and exited 139 with NO report at
     * all, turning a fault that named its own function into a silent one.
     *
     * The measurement stays in the shutdown report, where stdio is safe. To
     * get it at a fault, it needs a write(2) formatter of its own.
     */
    x86_diag_dump();
#ifdef X86_NATIVE_REACHED
    x86_reached_report();
#endif
#ifndef X86_NATIVE_TRACE
    fprintf(stderr,
        "[TRACE] BLIND SPOT: this build has X2_NATIVE_TRACE=OFF, so the ring "
        "above holds ONLY guest/host boundary crossings --\n"
        "[TRACE] not guest-to-guest calls, which are plain C calls and cross "
        "nothing. The faulting function is very likely NOT in it.\n"
        "[TRACE] Reconfigure with -DX2_NATIVE_TRACE=ON to record every "
        "recompiled body entry and exit.\n");
#endif
    _exit(3);
}

/* The runtime's hook (weak default in x86rt_native.c). */
const char *x86_poison_name(uint32_t addr, const char **mod)
{
    return poison_name(addr, mod);
}

/*
 * A run that has to be KILLED, reported.
 *
 * It is the one failure mode with nothing to read afterwards: a crash names a
 * body, an abort names a symbol, and a run that never returns leaves a log
 * that stops mid-sentence. tools/native_discover.sh sat on one round for fifty
 * minutes and then reported CONVERGENCE, because a killed run and a run that
 * found nothing look identical from outside.
 *
 * ASYNC-SIGNAL-SAFE, the hard way, because the obvious version did not work:
 * fprintf here deadlocks whenever the interrupted code happens to hold the
 * stdio lock, and it does often enough that the report was being cut off
 * halfway through its own first line -- a hang diagnostic that hangs. So the
 * message goes out with write(2), which cannot block on a lock, and the ring
 * dump (which uses stdio, and resolves a name per entry against 16k functions)
 * is BEST EFFORT behind an alarm: if it deadlocks or simply takes too long,
 * SIGALRM ends the process and the message is already out.
 */
static void interrupted(int sig)
{
    static const char msg[] =
        "\n*** x2native was INTERRUPTED -- it did not stop on its own.\n"
        "    Nothing below is a failure the run reported; this is where it\n"
        "    HAPPENED TO BE. A run that has to be killed is usually spinning:\n"
        "    read the ring below for a repeating pair of bodies.\n"
        "    (The ring is best-effort from a signal handler and may be cut\n"
        "    short; everything above this line is complete.)\n";
    ssize_t ignored;
    (void)sig;
    /* Back to the default first: a second signal must be able to kill this, or
       a report that itself hangs makes the process unkillable by the very
       timeout that was trying to bound it. */
    signal(SIGTERM, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    ignored = write(2, msg, sizeof msg - 1);
    (void)ignored;
    /*
     * Hand the reports to the heartbeat thread when there is one.
     *
     * Every report in this project is an atexit handler, because the run
     * always used to END. Now it reaches a frame loop and keeps going, so the
     * only way it stops is a kill -- and a signal handler cannot run those
     * reports: they are stdio, and stdio here deadlocks against whatever the
     * interrupted code was holding. The heartbeat thread is ordinary context.
     * The alarm is the backstop: if that thread never gets there, the process
     * still dies rather than becoming unkillable by the very kill that was
     * trying to stop it.
     */
    signal(SIGALRM, SIG_DFL);
    if (heartbeat_running()) {
        alarm(10);
        x2_report_now = 1;
        return;
    }
    alarm(5);
    x86_diag_dump();
    _exit(4);
}

/*
 * What a normal exit would print, from ordinary context.
 *
 * exit() itself is NOT used: it was tried, and it died in "terminate called
 * without an active exception" -- a C++ teardown in the graphics stack that
 * has nothing to do with the reports. So the reports are called directly and
 * the process leaves with _exit.
 */
/*
 * `killed` says WHY the run is stopping, and it decides whether the boundary
 * ring is dumped.
 *
 * For a run that had to be killed the ring is the whole point -- it is the only
 * thing that says where a spin was. For a run that stopped because it reached
 * X2_MAX_FRAMES there is nothing to diagnose, and dumping it is not merely
 * noise: resolving a name per entry against 16k functions took MINUTES, long
 * enough that the timeout killed the process during its own clean shutdown and
 * the run exited 124 after all. The first clean stop did exactly that.
 */
void x2_interrupt_reports(int killed)
{
    extern void d3d8_host_report(void);
    extern void guest_heap_report(void);
    extern void x86_fallback_report(void);
    extern void guest_thread_report(void);
    extern void k32_critsec_report(void);
    extern void dinput_device_report(void);
    extern void dinput_pad_report(void);
    x86_fallback_report();
    d3d8_host_report();
    guest_heap_report();
    /* The threads and their critical sections are reported on EVERY ending,
       not only on a kill. They lived in x86_diag_dump, which the clean
       X2_MAX_FRAMES stop deliberately skips -- so the runs that WORK, the ones
       a scheduling change has to be judged on, produced no thread numbers at
       all. A counter you only see when the run failed cannot tell you the
       change helped. */
    guest_thread_report();
    guest_engine_thread_report();
    k32_critsec_report();
    /* The input reports too, and for the same reason: they were registered
       with atexit, and the clean frame-limit stop leaves through _exit -- so
       on precisely the runs that WORK, the numbers that say whether the game
       ever polled the pad were never printed. */
    dinput_device_report();
    dinput_pad_report();
    { extern void pad_glyphs_report(void); pad_glyphs_report(); }
    { extern void xbox_defaults_report(void); xbox_defaults_report(); }
    { extern void controller_defaults_ui_report(void); controller_defaults_ui_report(); }
    { extern void dsound_report(void); dsound_report(); }
    { extern void k32_asset_report(void), ws2_report(void);
      k32_asset_report(); ws2_report(); }
    { extern void conversation_report(void); conversation_report(); }
    { extern void script_trace_report(void); script_trace_report(); }
    { extern void x86_record_report(void); x86_record_report(); }
    { extern void x86_profiler_report(void); x86_profiler_report(); }
    /* shell32's save-path report was registered with atexit, and the clean
       X2_MAX_FRAMES stop leaves through _exit -- so on precisely the runs
       that reach gameplay it had never printed once. Same defect the input
       reports had; same fix. */
    { extern void shell32_report(void); shell32_report(); }
    /* And the oracle probe stream, for the third time the same reason: it was
       registered with atexit, and the clean X2_MAX_FRAMES stop leaves through
       _exit -- so on precisely the runs that reach gameplay, the ones a
       capture is taken from, the per-probe counts never printed. The stream
       itself survives either way (it is flushed as it is written), but
       "slerp 75260, normalize 0" is the part that says WHICH probes the run
       actually exercised, and a capture whose probes all read zero must be
       visible here rather than discovered at the comparison. */
    { extern void oracle_probe_report(void); oracle_probe_report(); }
    { extern void d3d8_vsconst_caller_report(void);
      d3d8_vsconst_caller_report(); }
    x86_epcount_report();
    fflush(stdout);
    if (killed)
        x86_diag_dump();
    else
        printf("  (the boundary ring is not dumped: this run stopped because "
               "it reached X2_MAX_FRAMES, so there is no spin to locate.)\n");
    fflush(stdout);
}

static int poison_init(void)
{
    struct sigaction sa;
    void *p = mmap((void *)(uintptr_t)POISON_BASE, POISON_SIZE, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (p == MAP_FAILED || (uintptr_t)p != POISON_BASE) {
        fprintf(stderr, "x2native: could not reserve the unbound-import page; "
                        "unresolved imports would read as plausible values\n");
        return -1;
    }
    /* On an alternate stack, so a fault caused by the guest stack running out
       -- or by runaway recursion in the runtime itself -- can still be
       reported. Without it the handler needs the very stack that just died and
       the process dumps core silently, which is how an infinite
       import-dispatch loop first appeared: as nothing at all. */
    {
        static char altstack[65536];   /* >= SIGSTKSZ on every target here */
        stack_t ss;
        ss.ss_sp = altstack;
        ss.ss_size = sizeof altstack;
        ss.ss_flags = 0;
        if (sigaltstack(&ss, NULL) != 0)
            fprintf(stderr, "x2native: no alternate signal stack; a stack "
                            "overflow will die silently\n");
    }
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_report;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    if (sigaction(SIGSEGV, &sa, NULL) != 0) return -1;
    /*
     * The OTHER fatal signals, which used to kill the run in silence.
     *
     * SIGSEGV was handled because unbound imports fault there, and that made
     * every other fault invisible: an illegal instruction (a jump through a
     * wrong function pointer, a RET onto a corrupted stack) produced no line
     * at all, so a crash and a closed window read the same from a log. Each of
     * these prints the same context a SIGSEGV does. `--fault-selftest` proves
     * every one of them fires.
     *
     * SIGABRT is deliberately NOT taken: this port's own aborts already name
     * themselves on the way out, and exit 134 is what the gates read.
     */
    {
        static const int fatal[] = { SIGILL, SIGFPE, SIGBUS, SIGTRAP };
        size_t i;
        for (i = 0; i < sizeof fatal / sizeof fatal[0]; i++)
            if (sigaction(fatal[i], &sa, NULL) != 0)
                fprintf(stderr, "x2native: could not install the fault "
                                "reporter for %s; a fault of that kind will "
                                "die silently\n", fault_name(fatal[i]));
    }
    /*
     * A HANG had no report at all, and that is the one failure mode with
     * nothing to read afterwards: a crash names a body, an abort names a
     * symbol, and a run that simply never returns produces a log that stops
     * mid-sentence. tools/native_discover.sh sat on one round for fifty
     * minutes and then reported CONVERGENCE, because a killed run and a run
     * that found nothing look identical from outside.
     *
     * So SIGTERM and SIGINT dump the boundary ring on the way out -- the same
     * thing a fault prints, which is what says WHERE the run was spinning.
     * timeout(1) sends SIGTERM, so this is what the loop now gets.
     */
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = interrupted;
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    return 0;
}

/*
 * PROOF THAT THE FAULT REPORTER FIRES -- x2native --fault-selftest.
 *
 * A crash reporter that never runs is worse than none: it makes silence read
 * as "no crash". Each signal is raised in a CHILD with stderr on a pipe, and
 * the parent requires the report to name that signal; a control child that
 * installs the same handlers and does NOT fault must produce nothing, so a
 * check that would pass on any output fails here.
 *
 * SIGILL is raised with a real illegal instruction (__builtin_trap emits UD2),
 * which is the exact shape of the crash this was written for. The other three
 * use raise(), which proves the handler and its message but carries si_code
 * SI_USER rather than a hardware code -- said here rather than implied.
 */
static int fault_child(int sig, int genuine, int control)
{
    struct sigaction sa;
    static const int fatal[] = { SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGTRAP };
    size_t i;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = fault_report;
    sa.sa_flags = SA_SIGINFO;
    for (i = 0; i < sizeof fatal / sizeof fatal[0]; i++)
        sigaction(fatal[i], &sa, NULL);
    if (control) { fflush(NULL); _exit(0); }
    if (genuine) __builtin_trap();
    raise(sig);
    fflush(NULL);
    _exit(0);                    /* the handler _exit(3)s; reaching here fails */
}

static int fault_selftest(void)
{
    static const struct { int sig; int genuine; const char *what; } cases[] = {
        { SIGILL,  1, "a real UD2 illegal instruction" },
        { SIGFPE,  0, "raise(SIGFPE)" },
        { SIGBUS,  0, "raise(SIGBUS)" },
        { SIGTRAP, 0, "raise(SIGTRAP)" },
        { SIGSEGV, 0, "raise(SIGSEGV)" },
    };
    size_t i;
    int fails = 0;

    for (i = 0; i <= sizeof cases / sizeof cases[0]; i++) {
        int control = (i == sizeof cases / sizeof cases[0]);
        int sig = control ? 0 : cases[i].sig;
        const char *want = control ? NULL : fault_name(sig);
        char buf[8192];
        int fd[2], status = 0;
        pid_t pid;
        size_t got = 0;
        ssize_t n;

        if (pipe(fd) != 0) {
            printf("x2native --fault-selftest: pipe() failed; NOTHING was "
                   "checked.\n");
            return 1;
        }
        fflush(NULL);
        pid = fork();
        if (pid < 0) {
            printf("x2native --fault-selftest: fork() failed; NOTHING was "
                   "checked.\n");
            return 1;
        }
        if (pid == 0) {
            close(fd[0]);
            dup2(fd[1], 2);
            close(fd[1]);
            return fault_child(sig, control ? 0 : cases[i].genuine, control);
        }
        close(fd[1]);
        /* Drain to EOF even once the buffer is full: a child blocked writing
           into a pipe nobody is reading would make waitpid() below hang, and a
           selftest that hangs is worse than one that fails. */
        for (;;) {
            char sink[4096];
            if (got < sizeof buf - 1)
                n = read(fd[0], buf + got, sizeof buf - 1 - got);
            else
                n = read(fd[0], sink, sizeof sink);
            if (n <= 0) break;
            if (got < sizeof buf - 1) got += (size_t)n;
        }
        buf[got] = 0;
        close(fd[0]);
        waitpid(pid, &status, 0);

        if (control) {
            int quiet = (strstr(buf, "***") == NULL);
            int clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
            printf("  control: handlers installed, no fault  -- %s "
                   "(%zu byte(s) on stderr, exit %d)\n",
                   quiet && clean ? "silent, as it must be"
                                  : "FAILED: it reported a fault that did not "
                                    "happen",
                   got, WIFEXITED(status) ? WEXITSTATUS(status) : -1);
            if (!quiet || !clean) fails++;
            continue;
        }
        {
            int named = strstr(buf, want) != NULL;
            int reported = WIFEXITED(status) && WEXITSTATUS(status) == 3;
            printf("  %-8s via %-32s -- %s (exit %d, %zu byte(s) reported)\n",
                   want, cases[i].what,
                   named && reported ? "reported by name"
                                     : "FAILED: no report reached stderr",
                   WIFEXITED(status) ? WEXITSTATUS(status)
                                     : -WTERMSIG(status), got);
            if (!named || !reported) fails++;
        }
    }
    printf("x2native --fault-selftest: %s -- %d failure(s). Before this, only "
           "SIGSEGV was handled and every other fatal signal killed the run "
           "with nothing printed.\n", fails ? "FAILED" : "PASSED", fails);
    return fails ? 1 : 0;
}

/* Each module's base, defined by its generated native file. The host maps the
   images and fills these in; nothing else knows where a module went. */
extern uint32_t g_imgbase_libIGDisplay;

/* The battery talks in libIGDisplay's guest addresses, so it needs that
   module's base to turn one into the address the dispatcher uses. */
#define DISP(ep) (g_imgbase_libIGDisplay + ((ep) - 0x10000000u))

static int x86_native_call(uint32_t ep, CPU *C)
{
    return x86_native_call_at(DISP(ep), C);
}

static const char *x86_native_name(uint32_t ep)
{
    return x86_native_name_at(DISP(ep));
}

void x86_seg_unset(const char *seg)
{
    fprintf(stderr, "x86_seg_unset: guest code read %s-relative memory before "
                    "the native host set a %s base. On Windows this is the "
                    "TIB; here it has to be modelled, and it has not been "
                    "yet.\n", seg, seg);
    abort();
}

/* Guest memory, as the guest addresses it. */
static uint32_t gr32(uint32_t a) { return *(volatile uint32_t *)(uintptr_t)a; }
static void gw32(uint32_t a, uint32_t v) { *(volatile uint32_t *)(uintptr_t)a = v; }
static uint8_t gr8(uint32_t a) { return *(volatile uint8_t *)(uintptr_t)a; }
static void gw8(uint32_t a, uint8_t v) { *(volatile uint8_t *)(uintptr_t)a = v; }

/* ---- the thread block (FS) --------------------------------------------
 *
 * libIGCore's code touches FS:[0] twelve times and nothing else -- the head of
 * the SEH exception-registration chain, from MSVC's try/except prologue
 * (`mov eax, fs:[0]` / `mov fs:[0], esp`). So FS needs one real word of guest
 * memory, not a stub: the chain is a linked list living in the guest's own
 * stack frames, and maintaining it correctly costs nothing.
 *
 * What this does NOT provide is exception DELIVERY. If guest code ever raises
 * an exception expecting the chain to be walked, nothing here walks it. That
 * is a real gap and it is recorded rather than papered over -- the chain being
 * well-formed is necessary for the prologues to run, not sufficient for SEH.
 */
#define TIB_BASE 0x000A0000u
#define TIB_SIZE 0x1000u

static int tib_init(void)
{
    if (pe_map_anon_low(TIB_BASE, TIB_SIZE) != 0) return -1;
    g_fsbase = TIB_BASE;
    /* Win32's end-of-chain sentinel. A zero here would look like a valid
       record at address 0 to anything that did walk the chain. */
    *(volatile uint32_t *)(uintptr_t)TIB_BASE = 0xFFFFFFFFu;
    return 0;
}

/* ---- the guest stack ---------------------------------------------------
 *
 * Ours, not the C stack. This is the one place the native build is simpler
 * than the Wine-hosted one: there, recompiled code is entered BY Windows code
 * and the guest stack is the real thread stack, which is what let host callees
 * run their frames over the runtime's own state (C080). Here the two are
 * different memory by construction.
 */
#define GUEST_STACK 0x00100000u
/* The runtime's own memory lives above everything the guest asks for. */
#define X2_RUNTIME_BASE 0x70000000u
static uint32_t guest_stack_top;

static int guest_stack_init(void)
{
    /* Below 4 GB like everything else the guest addresses, and mapped rather
       than malloc'd so its address is predictable in a fault report. */
    /* High, deliberately. The game manages its own address space and walks
       upward from just above its image reserving arenas; anything of ours in
       that path collides with it. Measured: with the stack at 0x30000000 the
       guest's arena walk ran straight into it. */
    if (pe_map_anon_low(X2_RUNTIME_BASE, GUEST_STACK) != 0) return -1;
    guest_stack_top = X2_RUNTIME_BASE + GUEST_STACK - 64u;
    return 0;
}

/* ---- module initialisation --------------------------------------------
 *
 * A DLL's globals are built by its entry point: the Windows loader calls
 * DllMainCRTStartup, which runs _initterm over the static-constructor tables
 * and then DllMain. Natively there is no loader, so nothing had run them --
 * which is why a cross-module call reached libIGCore's igGetMemoryPool and
 * found a null memory-pool table.
 *
 * Order matters: a module's constructors can call into another module, so
 * dependencies go first. That is read from the import tables rather than
 * hardcoded, and a cycle is reported rather than resolved arbitrarily.
 */
#define DLL_PROCESS_ATTACH 1

static int module_init_one(X86Module *m)
{
    CPU C;
    uint32_t entry = *m->base + pe_entry_rva(*m->base);
    if (!pe_is_dll(*m->base)) {
        /* An EXE's entry point is the program itself. Running it here would
           mean "initialising" a module by playing the game, which is a very
           different act from calling DllMain -- so it is a separate, explicit
           step (--run), not something module init does on the way past. */
        printf("  skip %-18s it is an EXE; its entry point is the program "
               "(use --run)\n", m->name);
        return 0;
    }
    const char *nm = x86_native_name_at(entry);
    if (!nm) {
        /* Reported in the SAME format the constructor-target list uses, so
           tools/native_discover.sh picks it up and seeds it without needing to
           know that entry points are a separate case. One report shape, one
           loop. */
        fprintf(stderr, "\n*** module entry point with no recompiled body.\n"
                        "    Static analysis did not mark it as code. Seed it "
                        "and re-lift; the address is the module's own.\n");
        fprintf(stderr, "    %-18s 0x%08x\n", m->name,
                m->preferred + (entry - *m->base));
        fprintf(stderr, "*** 1 of 1 entry point is missing a body\n");
        return -1;
    }
    /* __stdcall DllMain(hinstDLL, fdwReason, lpvReserved): three arguments
       plus the return address, and the callee pops them. */
    cpu_reset(&C);
    C.esp = guest_stack_top - 16u;
    gw32(C.esp +  0u, 0xDEADBEEFu);
    gw32(C.esp +  4u, *m->base);              /* hinstDLL IS the image base */
    gw32(C.esp +  8u, DLL_PROCESS_ATTACH);
    gw32(C.esp + 12u, 0);
    printf("  init %-18s entry 0x%08x %s\n", m->name, entry, nm);
    if (!x86_native_call_at(entry, &C)) {
        fprintf(stderr, "module_init: %s entry vanished between lookup and "
                        "call\n", m->name);
        return -1;
    }
    printf("       returned eax=0x%08x\n", C.eax);
    return C.eax ? 0 : -1;
}

static int modules_init(void)
{
    /* One slot per module that can be linked in. It was 16, which the set
       outgrew the moment cg, cgD3D8, libIGAudio and libIGCollision joined it --
       and "too many modules" named no number, so it read as a design limit
       rather than an array to widen. Same bound as `imgs` in main(). */
#define MAX_INIT_MODULES 24
    X86Module *m, *d;
    int done[MAX_INIT_MODULES], n = 0, i, pass, inited = 0, total = 0;
    X86Module *list[MAX_INIT_MODULES];
    for (m = x86_modules(); m; m = m->next) {
        if (total == MAX_INIT_MODULES) {
            fprintf(stderr, "module_init: more than %d modules are linked; "
                            "raise MAX_INIT_MODULES in src/native/x2native.c. "
                            "Nothing was initialised.\n", MAX_INIT_MODULES);
            return -1;
        }
        list[total] = m; done[total] = 0; total++;
    }
    for (pass = 0; pass < total; pass++) {
        int progress = 0;
        for (i = 0; i < total; i++) {
            int ready = 1;
            if (done[i]) continue;
            for (n = 0; n < total; n++) {
                if (n == i || done[n]) continue;
                if (pe_imports_module(*list[i]->base, list[n]->name)) ready = 0;
            }
            if (!ready) continue;
            if (module_init_one(list[i]) != 0) return -1;
            done[i] = 1; progress = 1; inited++;
        }
        if (!progress) break;
    }
    if (inited != total) {
        fprintf(stderr, "module_init: %d of %d modules initialised; the rest "
                        "import each other in a cycle, which this cannot "
                        "order\n", inited, total);
        return -1;
    }
    return 0;
}

/* ---- the battery -------------------------------------------------------
 *
 * Real postconditions, derived from the guest instructions, not "it did not
 * crash". Each case POISONS what it is going to check first, so a body that
 * did nothing at all fails instead of inheriting a value that was already
 * right -- which is the way a battery like this usually lies.
 *
 * Every case also reads or writes the MAPPED IMAGE. Leaf functions that only
 * touch registers would pass against a blank page and would say nothing about
 * the thing this binary exists to establish.
 */
#define SCRATCH (X2_RUNTIME_BASE + 0x00200000u)          /* a guest-addressable scratch object */

static int fails;
/*
 * The negative control, in the shipping binary.
 *
 * A battery whose checks all pass is indistinguishable from a battery whose
 * expectations do not bind -- poison a value, forget to run the body, and
 * "0 FAILED" reads exactly the same. --selftest skips one body ON PURPOSE: the
 * poisoned values must then be reported as failures, and if they are not, every
 * other line of output this run is worthless and it says so.
 */

/*
 * PROOF THAT THE OVERRIDE RESOLVER FIRES -- x2native --override-selftest.
 *
 * The resolver is what stands between a registered override and one that never
 * runs. Every override in the tree resolves, so the accepting path is
 * exercised constantly and the REJECTING paths never are -- which is exactly
 * the shape of a check nobody has seen work. This feeds it one case that must
 * be accepted and three that must be rejected, and reports the count either
 * way. It runs after the modules are mapped, because the resolver's whole job
 * is to consult them.
 */
static int override_selftest(void)
{
    struct { const char *module; uint32_t ep; int want_ok; const char *what; }
    cases[] = {
        { "XMen2.exe",       0x00617480u, 1, "a real override entry point" },
        { "NoSuchModule.dll",0x00401000u, 0, "a module that is not mapped" },
        { "XMen2.exe",       0xf0000000u, 0, "an address outside the image" },
        { "XMen2.exe",       0x00617481u, 0, "a mid-function address" },
    };
    int i, fails = 0;
    int n = (int)(sizeof cases / sizeof cases[0]);

    for (i = 0; i < n; i++) {
        char why[256] = "";
        uint32_t mapped = 0;
        int rc = x86_override_resolve_check(cases[i].module, cases[i].ep,
                                            &mapped, why, sizeof why);
        int ok = (rc == 0);
        if (ok != cases[i].want_ok) {
            printf("  FAIL  %-28s %s 0x%08x: expected %s, got %s%s%s\n",
                   cases[i].what, cases[i].module, cases[i].ep,
                   cases[i].want_ok ? "ACCEPT" : "REJECT",
                   ok ? "ACCEPT" : "REJECT",
                   ok ? "" : " -- ", ok ? "" : why);
            fails++;
        } else if (ok) {
            printf("  ok    %-28s %s 0x%08x -> mapped 0x%08x\n",
                   cases[i].what, cases[i].module, cases[i].ep, mapped);
        } else {
            printf("  ok    %-28s rejected: %s\n", cases[i].what, why);
        }
    }
    printf("x2native --override-selftest: %s (%d of %d case(s) failed). "
           "%d registered override(s) are live in this build.\n",
           fails ? "FAILED" : "PASSED", fails, n, x86_override_count());
    return fails;
}

static int selftest, skip_body;

static int call_body(uint32_t ep, CPU *C)
{
    if (skip_body) return 1;              /* pretend it ran; nothing changes */
    return x86_native_call(ep, C);
}

static int checks;

static void check(const char *what, uint32_t got, uint32_t want)
{
    checks++;
    if (got == want) {
        printf("    ok    %-34s 0x%08x\n", what, got);
    } else {
        printf("    FAIL  %-34s got 0x%08x, want 0x%08x\n", what, got, want);
        fails++;
    }
}


/* Set up a guest frame: return address plus `nargs` stack arguments. */
static void frame(CPU *C, const uint32_t *args, int nargs)
{
    int i;
    cpu_reset(C);
    C->esp = guest_stack_top - (uint32_t)(nargs + 1) * 4u;
    gw32(C->esp, 0xDEADBEEFu);                    /* the return address */
    for (i = 0; i < nargs; i++) gw32(C->esp + 4u + (uint32_t)i * 4u, args[i]);
}

static void case_enumerate(void)
{
    CPU C; uint32_t args[2] = { 0, 0 }, esp0;
    /* MOV byte [0x10021d4c],1 ; XOR EAX,EAX ; RET 8 */
    printf("  0x10005660 igWin32Window::enumerateMouseAndKeyboard\n");
    gw8(g_imgbase + 0x21d4cu, 0xA5);              /* poison */
    frame(&C, args, 2);
    esp0 = C.esp;
    C.eax = 0x11223344u;                          /* must be zeroed by XOR */
    if (!call_body(0x10005660u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    check("byte [imgbase+0x21d4c]", gr8(g_imgbase + 0x21d4cu), 1u);
    check("eax after XOR EAX,EAX", C.eax, 0u);
    check("esp delta (RET 8 = 4+8)", C.esp - esp0, 12u);
}

static void case_getscreensize(void)
{
    CPU C; uint32_t args[2] = { SCRATCH, SCRATCH + 16u }, esp0;
    /* [arg0] = [0x10021d54] ; [arg1] = [0x10021d58] ; RET 8 */
    printf("  0x10006280 igWin32Window::getScreenSize\n");
    gw32(g_imgbase + 0x21d54u, 0x00000320u);      /* poison the globals with */
    gw32(g_imgbase + 0x21d58u, 0x00000258u);      /* 800 and 600 */
    gw32(SCRATCH, 0xBADF00Du);
    gw32(SCRATCH + 16u, 0xBADF00Du);
    frame(&C, args, 2);
    esp0 = C.esp;
    if (!call_body(0x10006280u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    check("*out_width  <- [0x10021d54]", gr32(SCRATCH), 0x320u);
    check("*out_height <- [0x10021d58]", gr32(SCRATCH + 16u), 0x258u);
    check("esp delta (RET 8 = 4+8)", C.esp - esp0, 12u);
}

static void case_findmouse(void)
{
    CPU C; uint32_t args[2] = { SCRATCH, 0 }, esp0; int i;
    /* four dwords from [arg0+0x14 ..] into four image globals; RET 8 */
    printf("  0x100051c0 igWin32ControllerManager::findMouse\n");
    for (i = 0; i < 4; i++) gw32(SCRATCH + 0x14u + (uint32_t)i * 4u, 0xC0DE0000u + (uint32_t)i);
    for (i = 0; i < 4; i++) gw32(g_imgbase + 0x21b34u + (uint32_t)i * 4u, 0xBADF00Du);
    frame(&C, args, 2);
    esp0 = C.esp;
    /* Poisoned, or "eax is 0" would be true before the body ran as well --
       --selftest caught exactly that: 13 of 14 checks bound, this one did not. */
    C.eax = 0x11223344u;
    if (!call_body(0x100051c0u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    for (i = 0; i < 4; i++) {
        char lbl[64];
        snprintf(lbl, sizeof lbl, "[imgbase+0x%x] <- [obj+0x%x]",
                 0x21b34u + (unsigned)i * 4u, 0x14u + (unsigned)i * 4u);
        check(lbl, gr32(g_imgbase + 0x21b34u + (uint32_t)i * 4u), 0xC0DE0000u + (uint32_t)i);
    }
    check("eax after XOR EAX,EAX", C.eax, 0u);
    check("esp delta (RET 8 = 4+8)", C.esp - esp0, 12u);
}

static void case_arkinit(void)
{
    CPU C;
    /* EAX = [0x10021b80] ; [EAX+0x3c] = 0x10002370 (image-relative immediate) */
    printf("  0x10002cc0 igWindow::arkRegisterInitialize\n");
    gw32(g_imgbase + 0x21b80u, SCRATCH);          /* the class meta object */
    gw32(SCRATCH + 0x3cu, 0xBADF00Du);
    frame(&C, NULL, 0);
    if (!call_body(0x10002cc0u, &C)) { printf("    FAIL  not in table\n"); fails++; return; }
    check("eax <- [0x10021b80]", C.eax, SCRATCH);
    /* The point of this one: an immediate holding an image address must be
       REBASED to where we mapped it, not emitted as the literal 0x10002370. */
    check("[meta+0x3c] rebased immediate", gr32(SCRATCH + 0x3cu), g_imgbase + 0x2370u);
}

/*
 * The import ABI, which is the easiest thing here to get quietly wrong.
 *
 * A recompiled body calls an import with a fake return address on the guest
 * stack; the implementation has to leave ESP where the real callee would.
 * Win32 is __stdcall (callee pops the arguments), the CRT is __cdecl (it does
 * not). Confusing the two shifts the guest stack by a word per call and the
 * damage shows up somewhere with no connection to the cause, so both are
 * checked here against a known-answer call rather than reasoned about.
 */
void imp_KERNEL32_GetModuleHandleA(CPU *C);
void imp_KERNEL32_LoadLibraryA(CPU *C);
void imp_KERNEL32_GetProcAddress(CPU *C);
void imp_USER32_MapVirtualKeyA(CPU *C);
void imp_DINPUT8_DirectInput8Create(CPU *C);
void imp_MSVCR71___RTDynamicCast(CPU *C);
void imp_MSVCR71_qsort(CPU *C);
void imp_KERNEL32_MultiByteToWideChar(CPU *C);
void imp_KERNEL32_FindFirstFileA(CPU *C);
void imp_KERNEL32_FindNextFileA(CPU *C);
void imp_KERNEL32_FindClose(CPU *C);
void imp_MSVCRT_malloc(CPU *C);
void imp_MSVCRT_free(CPU *C);

/* Call an import the way a recompiled body does. Returns the ESP delta. */
static uint32_t call_import(void (*fn)(CPU *), CPU *C, const uint32_t *args,
                            int nargs)
{
    uint32_t esp0;
    int i;
    C->esp = guest_stack_top - (uint32_t)(nargs + 1) * 4u;
    gw32(C->esp, 0xDEADBEEFu);                     /* the fake return address */
    for (i = 0; i < nargs; i++) gw32(C->esp + 4u + (uint32_t)i * 4u, args[i]);
    esp0 = C->esp;
    if (!skip_body) fn(C);
    return C->esp - esp0;
}

static void case_import_abi(void)
{
    CPU C; uint32_t d;
    printf("  native import ABI (stdcall vs cdecl stack cleanup)\n");

    {   /* KERNEL32!GetModuleHandleA(NULL) -- __stdcall, 1 argument.
           The module's own handle IS its image base in a PE. */
        uint32_t args[1] = { 0 };
        cpu_reset(&C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_KERNEL32_GetModuleHandleA, &C, args, 1);
        check("GetModuleHandleA(NULL) -> imgbase", C.eax, g_imgbase);
        check("  stdcall esp delta (4+1*4)", d, 8u);
    }
    {   /* A module that is NOT in the address space.
     *
     * NULL is Win32's own answer and the game is written for it: it probes
     * `GetModuleHandleA("d3d8d.dll")` -> LoadLibraryA -> GetProcAddress
     * ("DebugSetMute") to pick up the D3D8 DEBUG runtime if it happens to be
     * loaded, and skips the whole block when the first call returns 0. This
     * used to abort() -- treating an ordinary negative answer as fatal and
     * stopping the run on a debug convenience the game does not need.
     *
     * Checked with a name that must NEVER resolve, so the check cannot pass
     * by accident on a host that gained the module. */
        static const char miss[] = "d3d8d.dll";
        uint32_t nm = SCRATCH + 0x300u, args[1];
        unsigned i;
        for (i = 0; i < sizeof miss; i++)
            *(volatile uint8_t *)(uintptr_t)(nm + i) = (uint8_t)miss[i];
        args[0] = nm;
        cpu_reset(&C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_KERNEL32_GetModuleHandleA, &C, args, 1);
        check("GetModuleHandleA(\"d3d8d.dll\") -> 0, not an abort", C.eax, 0);
        check("  stdcall esp delta (4+1*4)", d, 8u);
    }
    {   /* A module that IS here. GetModuleHandleA and LoadLibraryA must agree
     * about what is in the address space -- they were separate implementations
     * with separate ideas of it, which is how the abort above survived. */
        static const char have[] = "libIGCore.dll";
        uint32_t nm = SCRATCH + 0x320u, args[1], viaload;
        unsigned i;
        for (i = 0; i < sizeof have; i++)
            *(volatile uint8_t *)(uintptr_t)(nm + i) = (uint8_t)have[i];
        args[0] = nm;
        cpu_reset(&C);
        C.eax = 0xBADF00Du;                  /* so the SKIPPED pass fails too */
        call_import(imp_KERNEL32_LoadLibraryA, &C, args, 1);
        viaload = C.eax;
        cpu_reset(&C);
        C.eax = 0xBADD00Du;                  /* a DIFFERENT sentinel: equal
                                                sentinels would agree when
                                                neither call ran */
        call_import(imp_KERNEL32_GetModuleHandleA, &C, args, 1);
        check("GetModuleHandleA agrees with LoadLibraryA on a mapped module",
              C.eax, viaload);
    }
    {   /* KERNEL32!MultiByteToWideChar -- __stdcall, 6 arguments. Measuring
           form first (cchWideChar 0 returns the length needed). */
        static const char msg[] = "SDL3";
        uint32_t src = SCRATCH + 0x100u, dst = SCRATCH + 0x200u;
        uint32_t args[6] = { 0, 0, 0, 0xFFFFFFFFu, 0, 0 };
        unsigned i;
        for (i = 0; i < sizeof msg; i++)
            *(volatile uint8_t *)(uintptr_t)(src + i) = (uint8_t)msg[i];
        args[2] = src; args[4] = dst;
        cpu_reset(&C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_KERNEL32_MultiByteToWideChar, &C, args, 6);
        check("MB2WC measure -> strlen+1", C.eax, 5u);
        check("  stdcall esp delta (4+6*4)", d, 28u);

        args[5] = 16;                              /* now actually convert */
        for (i = 0; i < 8; i++) gw32(dst + i * 4u, 0xBADF00Du);
        cpu_reset(&C);
        d = call_import(imp_KERNEL32_MultiByteToWideChar, &C, args, 6);
        check("MB2WC convert -> count", C.eax, 5u);
        check("  wide 'S'", *(volatile uint16_t *)(uintptr_t)dst, (uint16_t)'S');
        check("  wide 'D'", *(volatile uint16_t *)(uintptr_t)(dst + 2u), (uint16_t)'D');
        check("  wide NUL terminator", *(volatile uint16_t *)(uintptr_t)(dst + 8u), 0u);
    }
    {   /* MSVCRT!malloc/free -- __cdecl, so the CALLER cleans up and esp moves
           by the return address only. This is the case that would silently
           differ from the Win32 ones. */
        uint32_t args[1] = { 64 }, p;
        cpu_reset(&C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_MSVCRT_malloc, &C, args, 1);
        p = C.eax;
        check("malloc(64) != 0", p != 0u && p != 0xBADF00Du, 1u);
        check("  cdecl esp delta (4 only)", d, 4u);
        if (p && p != 0xBADF00Du) {
            args[0] = p;
            cpu_reset(&C);
            d = call_import(imp_MSVCRT_free, &C, args, 1);
            check("  free cdecl esp delta (4)", d, 4u);
        }
    }
}

/*
 * The point of linking two modules: a call that leaves one and lands in the
 * other, with no Wine and no original code anywhere.
 *
 * igWindow::getClassTypeLazy reads the class meta slot, and when it is empty
 * calls libIGCore's igGetMemoryPool and _instantiateFromPool through the IAT.
 * Poisoning the slot first means the cross-module path MUST be taken -- if the
 * import were unbound this faults by name instead of quietly returning.
 */
extern uint32_t g_imgbase_libIGCore;

/* ---- the engine's 4x4 matrix multiply ----------------------------------
 *
 * Every animated character's bone palette is built by concatenating parent
 * transforms through igMatrix44f::multiplyAligned, and issue #80 measured the
 * palette arriving at the renderer NON-RIGID: 6 of 32 bones were rotations in
 * the port where 32 of 32 were in the stock game. A matrix whose rows are not
 * unit length is not a rotation, so that is a defect and not two runs sitting
 * on different animation frames.
 *
 * multiplyAligned does not call a multiply directly. It calls through a
 * function pointer at 0x1008a3c8 that FUN_10018e30 fills in from
 * igGetCPUCaps -- 3DNow!, SSE, or the x87 fallback -- so which code actually
 * runs is a run-time fact, and this case prints it BY NAME rather than
 * assuming. The 3DNow! body is deliberately never called: its PFMUL/PFADD are
 * untranslatable and abort by name, which is the correct behaviour and not
 * something to trip here.
 *
 * tests/test_sse.c already checks the SSE MODEL against the host's own SSE.
 * That is a different question from this one. This calls the EMITTED
 * TRANSLATION of the real function, so it also covers what recomp.py made of
 * these specific encodings -- SHUFPS with an immediate, MULPS against a memory
 * operand, MOVAPS to and from guest memory.
 *
 * The inputs are two rigid transforms, so the answer is known independently of
 * any backend: the product must be rigid too. That is the property that failed
 * in the game, checked here where it needs no game, no Wine and no oracle.
 */
extern uint32_t g_imgbase_libIGMath;

#define IGM_SSE      0x100192a0u        /* alignedMatrixMultiplySSE */
#define IGM_X87      0x100193a0u        /* matrixMultiply, the fallback */
#define IGM_3DNOW    0x100190d0u        /* alignedMatrixMultiply3dNow */
#define IGM_BACKEND  0x1008a3c8u        /* the pointer FUN_10018e30 fills in */
#define IGM(a)       (g_imgbase_libIGMath + ((a) - 0x10000000u))

/* Row-vector convention, as the SSE body's own shape shows: it broadcasts
   A[i][k] and scales B's row k, so dst[i][j] = sum_k A[i][k] * B[k][j]. */
static void mat_ref(float d[16], const float a[16], const float b[16])
{
    int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a[i * 4 + k] * b[k * 4 + j];
            d[i * 4 + j] = s;
        }
}

/* A rotation about `axis` by `ang`, with a translation in row 3. */
static void mat_rigid(float m[16], int axis, float ang, float tx, float ty, float tz)
{
    float c = cosf(ang), s = sinf(ang);
    int i;
    for (i = 0; i < 16; i++) m[i] = (i % 5) == 0 ? 1.0f : 0.0f;
    if (axis == 0) { m[5] = c; m[6] = s; m[9] = -s; m[10] = c; }
    if (axis == 1) { m[0] = c; m[2] = -s; m[8] = s;  m[10] = c; }
    if (axis == 2) { m[0] = c; m[1] = s; m[4] = -s; m[5] = c; }
    m[12] = tx; m[13] = ty; m[14] = tz;
}

/* The worst row-length error over the three basis rows. Zero for a rotation. */
static float mat_row_error(const float m[16])
{
    float worst = 0.0f;
    int i;
    for (i = 0; i < 3; i++) {
        float l = sqrtf(m[i * 4] * m[i * 4] + m[i * 4 + 1] * m[i * 4 + 1] +
                        m[i * 4 + 2] * m[i * 4 + 2]);
        float e = fabsf(l - 1.0f);
        if (e > worst) worst = e;
    }
    return worst;
}

static void case_matrix_multiply(void)
{
    /* Three 16-byte-aligned matrices; SCRATCH is page-aligned, so these are
       too, which MOVAPS requires on real silicon. */
    const uint32_t gA = SCRATCH + 0x300u, gB = SCRATCH + 0x340u,
                   gD = SCRATCH + 0x380u;
    float A[16], B[16], want[16];
    uint32_t args[3], backend;
    const char *nm;
    int pass;

    printf("  libIGMath igMatrix44f 4x4 multiply (issue #80: the bone palette)\n");

    if (g_imgbase_libIGMath == 0) {
        printf("    REFUSED  libIGMath is not mapped, so NOTHING below ran.\n"
               "             This case tests nothing without it; that is a\n"
               "             build that omitted the module, not a pass.\n");
        fails++; checks++;
        return;
    }

    /* Which body the engine actually installed. Printed unconditionally: an
       unexpected backend is the single most useful thing this case can say. */
    backend = gr32(IGM(IGM_BACKEND));
    nm = backend ? x86_native_name_at(backend) : NULL;
    printf("    backend pointer [0x1008a3c8] = 0x%08x  %s\n",
           backend, nm ? nm : "(no body at that address)");
    if (backend == IGM(IGM_3DNOW))
        printf("      ^ the 3DNow! body. Its PFMUL/PFADD are untranslatable,\n"
               "        so reaching it aborts by name -- see issue #80.\n");

    mat_rigid(A, 0, 0.7f,  1.0f,  2.0f,  3.0f);
    mat_rigid(B, 1, 1.3f, -4.0f,  5.0f, -6.0f);
    mat_ref(want, A, B);

    memcpy((void *)(uintptr_t)gA, A, sizeof A);
    memcpy((void *)(uintptr_t)gB, B, sizeof B);

    /* Both inputs are rigid, so the reference product is too. Check that here:
       if this ever fails the reference is wrong and every verdict below is
       meaningless, which must not be reported as a backend defect.
       Kept out of the skipped-body control on purpose: it does not depend on
       any body running, so skipping bodies could not falsify it. */
    if (!skip_body)
        check("reference product of two rigid inputs is rigid",
              mat_row_error(want) < 1e-5f, 1u);

    /* Each backend that has a body, called through the real dispatcher.
       cdecl: void f(Matrix *dst, const Matrix *a, const Matrix *b). */
    {
        static const struct { uint32_t ep; const char *name; } BODY[] = {
            { IGM_SSE, "alignedMatrixMultiplySSE" },
            { IGM_X87, "matrixMultiply (x87 fallback)" },
        };
        size_t i;
        for (i = 0; i < sizeof BODY / sizeof BODY[0]; i++) {
            CPU C;
            float got[16];
            float worst = 0.0f;
            char lbl[96];
            int j;

            /* Poison the destination, so "the body never ran" cannot read as
               agreement with the reference. */
            memset((void *)(uintptr_t)gD, 0x5A, 64);

            args[0] = gD; args[1] = gA; args[2] = gB;
            frame(&C, args, 3);
            if (skip_body) {
                /* the negative control: leave the poison in place */
            } else if (!x86_native_call_at(IGM(BODY[i].ep), &C)) {
                snprintf(lbl, sizeof lbl, "%s is in the table", BODY[i].name);
                check(lbl, 0u, 1u);
                continue;
            }
            memcpy(got, (const void *)(uintptr_t)gD, sizeof got);

            for (j = 0; j < 16; j++) {
                float e = fabsf(got[j] - want[j]);
                if (e > worst) worst = e;
            }
            pass = worst < 1e-4f;
            snprintf(lbl, sizeof lbl, "%s == reference", BODY[i].name);
            check(lbl, (uint32_t)pass, 1u);
            printf("      worst |got-want| over 16 element(s): %g\n",
                   (double)worst);
            snprintf(lbl, sizeof lbl, "%s product is rigid", BODY[i].name);
            check(lbl, mat_row_error(got) < 1e-4f, 1u);
            printf("      row lengths %.6f %.6f %.6f  (all 1.0 for a rotation)\n",
                   (double)sqrtf(got[0]*got[0] + got[1]*got[1] + got[2]*got[2]),
                   (double)sqrtf(got[4]*got[4] + got[5]*got[5] + got[6]*got[6]),
                   (double)sqrtf(got[8]*got[8] + got[9]*got[9] + got[10]*got[10]));
            if (!pass) {
                printf("      got  ");
                for (j = 0; j < 16; j++) printf("%9.5f%s", (double)got[j],
                                                (j % 4) == 3 ? "\n           " : " ");
                printf("\n      want ");
                for (j = 0; j < 16; j++) printf("%9.5f%s", (double)want[j],
                                                (j % 4) == 3 ? "\n           " : " ");
                printf("\n");
            }
        }
    }
}

static void case_cross_module(void)
{
    CPU C;
    uint32_t meta_slot = g_imgbase + 0x21b80u;
    const char *nm;
    printf("  cross-module call: libIGDisplay -> libIGCore\n");

    /* The import this path uses must be bound to a libIGCore body, not to a
       poison slot. Check that before relying on it, so a failure reads as
       "the import is unbound" rather than as a wrong result. */
    {
        uint32_t slot = gr32(g_imgbase + 0x91f4u);   /* _instantiateFromPool */
        nm = x86_native_name_at(slot);
        check("IAT slot -> a libIGCore body", nm != NULL, 1u);
        if (nm) printf("      bound to: %s\n", nm);
        check("  target is inside libIGCore",
              (slot >= g_imgbase_libIGCore) &&
              (slot - g_imgbase_libIGCore < 0x200000u), 1u);
    }
    /* Still not run end to end, and the reason has MOVED, which is the result
       of this build. It is no longer "no module initialisation exists": both
       modules now run their entry points and their static constructors, and
       DllMain returns TRUE for each. The path stops one layer further in, at
       libIGCore's igGetCurrentMemoryPoolContext returning 0 -- the engine's
       memory system, which XMen2.exe brings up as part of ENGINE init, not
       something a DLL's constructors do.
       Reaching into the engine's init sequence by picking exports that look
       right would be jumping ahead of the frontier, so it waits for rc-exe. */
    (void)meta_slot;
    printf("      transfer works; execution now stops in the ENGINE's memory\n"
           "      system, which the exe initialises (rc-exe), not in module init\n");
}

/*
 * The guest heap. Checked because a pointer that does not fit in 32 bits is
 * exactly the class of bug that survives review: it looks like a pointer, and
 * the truncation only shows up when something dereferences it.
 */
static void case_guest_heap(void)
{
    uint32_t a, b, c, used0, free0, blocks0, used1, free1, blocks1;
    printf("  guest heap\n");
    guest_heap_stats(&used0, &free0, &blocks0);
    a = guest_malloc(100);
    b = guest_malloc(200);
    check("malloc fits in a guest pointer", (a >> 24) != 0u && a < 0xFFFFFFFFu, 1u);
    check("two allocations differ", a != b && a != 0u && b != 0u, 1u);
    /* Writing through it must not disturb the other block. */
    memset((void *)(uintptr_t)a, 0xAB, 100);
    memset((void *)(uintptr_t)b, 0xCD, 200);
    check("no overlap after writes",
          *(volatile uint8_t *)(uintptr_t)a == 0xABu
          && *(volatile uint8_t *)(uintptr_t)(b + 199u) == 0xCDu, 1u);
    guest_free(a);
    guest_free(b);
    guest_heap_stats(&used1, &free1, &blocks1);
    check("free returns every byte", used1, used0);
    check("freed space coalesces back", free1, free0);
    c = guest_realloc(0, 64);
    memset((void *)(uintptr_t)c, 0x5A, 64);
    c = guest_realloc(c, 4096);                 /* forces a move + copy */
    check("realloc preserves contents",
          *(volatile uint8_t *)(uintptr_t)c == 0x5Au
          && *(volatile uint8_t *)(uintptr_t)(c + 63u) == 0x5Au, 1u);
    guest_free(c);
}

/*
 * The setjmp table gives slots back -- and only the right ones.
 *
 * Both directions are checked, because a reclaimer that frees everything and a
 * reclaimer that frees nothing both leave a table that "works" right up until
 * it does not: the first loses a buffer the guest still jumps to, the second
 * fills up and aborts sixteen resource loads in. Neither shows up in a run that
 * takes one setjmp.
 *
 * Driven through x86_setjmp_buf itself, at the guest ABI: ESP points at the
 * return address, and the jmp_buf address is the word above it. The host setjmp
 * is deliberately NOT taken -- this tests the bookkeeping, and a jmp_buf nobody
 * jumps to is never read.
 */
static void case_setjmp_table(void)
{
    uint32_t frame = guest_malloc(64), envs[4], stack_env;
    CPU C;
    int i, before, freed;

    printf("  setjmp buffer table\n");
    before = x86_setjmp_live();
    for (i = 0; i < 4; i++) {
        envs[i] = guest_malloc(64);
        cpu_reset(&C);
        C.esp = frame;
        WR32(frame, 0x00646b2cu);              /* the return address */
        WR32(frame + 4u, envs[i]);             /* _setjmp3's jmp_buf */
        x86_setjmp_buf(&C);
    }
    check("four buffers are held", (uint32_t)(x86_setjmp_live() - before), 4u);

    /* The same buffer again is the same slot, not a fifth. */
    cpu_reset(&C);
    C.esp = frame;
    WR32(frame, 0x00646b2cu);
    WR32(frame + 4u, envs[0]);
    x86_setjmp_buf(&C);
    check("the same jmp_buf reuses its slot",
          (uint32_t)(x86_setjmp_live() - before), 4u);

    /* Free two of the four objects. Only those two may be reclaimed: the
       other two are still allocated and the guest may still jump to them. */
    guest_free(envs[1]);
    guest_free(envs[3]);
    freed = x86_setjmp_reclaim();
    check("reclaims exactly the freed buffers", (uint32_t)freed, 2u);
    check("keeps the ones still allocated",
          (uint32_t)(x86_setjmp_live() - before), 2u);

    /*
     * A jmp_buf OUTSIDE the heap is never reclaimed, and this check exists
     * because the opposite rule was tried and the game refuted it: "below the
     * current ESP means the frame was popped" freed the buffer FUN_006460d1
     * took, and the guest then longjmp'd to it. Nothing outside the arena is
     * provably dead, so nothing outside the arena may be dropped.
     */
    stack_env = SCRATCH + 0x100u;
    cpu_reset(&C);
    C.esp = frame;
    WR32(frame, 0x00646b2cu);
    WR32(frame + 4u, stack_env);
    x86_setjmp_buf(&C);
    check("a non-heap buffer is never reclaimed",
          (uint32_t)x86_setjmp_reclaim(), 0u);
    check("and is still held afterwards",
          (uint32_t)(x86_setjmp_live() - before), 3u);

    guest_free(envs[0]);
    guest_free(envs[2]);
    x86_setjmp_reclaim();
    guest_free(frame);
}

/*
 * A module this host implements but nothing imports must be loadable BY PATH.
 *
 * Both halves failed before, and each on its own is enough to disable input
 * wholesale (issue #32): LoadLibraryA compared the caller's full path
 * "C:\...\dinput8.dll" against a bare module name and never matched, and even
 * a match would have found nothing, because GetProcAddress resolved only
 * symbols that appear in some mapped module's IMPORT table -- which a run-time
 * lookup never does.
 *
 * The negative is the point of the last check: a symbol this host does NOT
 * implement must still come back NULL, or "the module loaded" would mean
 * "every function in it exists".
 */
static void guest_stdcall2_probe(CPU *C)
{
    uint32_t a = RD32(C->esp + 4u), b = RD32(C->esp + 8u);
    C->eax = a ^ b;
    C->esp += 12u;                 /* return address + two stdcall arguments */
}

static void case_runtime_module(void)
{
    uint32_t path = guest_malloc(64), sym = guest_malloc(64), h, p, cb, top;
    CPU C;

    printf("  run-time module lookup\n");
    strcpy((char *)(uintptr_t)path, "C:\\Windows\\System32\\dinput8.dll");

    cpu_reset(&C);
    C.esp = SCRATCH + 0x200u;
    WR32(C.esp, 0);                                  /* return address */
    WR32(C.esp + 4u, path);
    imp_KERNEL32_LoadLibraryA(&C);
    h = C.eax;
    check("dinput8.dll loads by full path", h != 0u, 1u);

    strcpy((char *)(uintptr_t)sym, "DirectInput8Create");
    cpu_reset(&C);
    C.esp = SCRATCH + 0x200u;
    WR32(C.esp, 0);
    WR32(C.esp + 4u, h);
    WR32(C.esp + 8u, sym);
    imp_KERNEL32_GetProcAddress(&C);
    p = C.eax;
    check("DirectInput8Create resolves", p != 0u, 1u);
    check("and to the address it was published at",
          p, x86_native_export_addr("DINPUT8.DLL", "DirectInput8Create"));

    strcpy((char *)(uintptr_t)sym, "DirectInput8CreateNoSuchThing");
    cpu_reset(&C);
    C.esp = SCRATCH + 0x200u;
    WR32(C.esp, 0);
    WR32(C.esp + 4u, h);
    WR32(C.esp + 8u, sym);
    imp_KERNEL32_GetProcAddress(&C);
    check("a symbol this host lacks is still NULL", C.eax, 0u);

    /* Host->guest callbacks need the callee-cleaned byte count in their ABI
       contract. Without it, a correct RET 8 was reported as an imbalance and
       the copied CPU was repaired to the wrong post-call ESP. Drive the real
       dispatcher with both arguments present and check both the values and
       exact stack result. */
    cb = x86_native_callback(guest_stdcall2_probe, "battery",
                             "guest_stdcall2_probe", NULL);
    cpu_reset(&C);
    top = SCRATCH + 0x300u;
    C.esp = top - 8u;
    WR32(C.esp + 0u, 0x13579bdfu);
    WR32(C.esp + 4u, 0x2468ace0u);
    x86_guest_call_args(&C, cb, 8u);
    check("a RET 8 callback receives both arguments", C.eax,
          0x13579bdfu ^ 0x2468ace0u);
    check("and lands at the caller's exact ESP", C.esp, top);

    guest_free(path);
    guest_free(sym);
}

/*
 * The keyboard layout table, and the DirectInput device driven THROUGH its
 * vtable -- which is the path the guest takes, and the only one that proves
 * the slot numbering.
 *
 * The negatives are the reason this exists. A device that answers every call
 * with S_OK and a block of zeros looks exactly like a working one that nobody
 * is touching, so what is checked is the REFUSALS: a state read before Acquire,
 * an Acquire before the data format is known, and a size that disagrees with
 * the format the caller itself declared. Each of those has a correct answer
 * that is not success.
 */
static uint32_t com_call(uint32_t obj, int slot, const uint32_t *args, int n)
{
    CPU C;
    int i;
    uint32_t vt = RD32(obj);
    cpu_reset(&C);
    C.esp = SCRATCH + 0x400u - (uint32_t)(n + 2) * 4u;
    WR32(C.esp, 0xD1A10000u);                        /* return address */
    WR32(C.esp + 4u, obj);                           /* this */
    for (i = 0; i < n; i++) WR32(C.esp + 8u + (uint32_t)i * 4u, args[i]);
    x86_dispatch(&C, RD32(vt + (uint32_t)slot * 4u));
    return C.eax;
}

static void case_dinput(void)
{
    /* GUID_SysKeyboard, byte for byte as XMen2.exe holds it at 0x6a15e4. */
    static const unsigned char KBD[16] = {
        0x61,0x2B,0x1D,0x6F, 0xA0,0xD5, 0xCF,0x11,
        0xBF,0xC7, 0x44,0x45,0x53,0x54,0x00,0x00
    };
    uint32_t guid = guest_malloc(16), out = guest_malloc(4);
    uint32_t df = guest_malloc(24), state = guest_malloc(256);
    uint32_t args[4], di, dev, hr;
    CPU C;

    printf("  keyboard layout and DirectInput device\n");

    /* MapVirtualKeyA, the table the game builds its key names from. */
    cpu_reset(&C);
    C.esp = SCRATCH + 0x300u;
    WR32(C.esp, 0); WR32(C.esp + 4u, 0x1Eu); WR32(C.esp + 8u, 1u);
    imp_USER32_MapVirtualKeyA(&C);
    check("scancode 0x1e maps to VK 'A'", C.eax, (uint32_t)'A');

    cpu_reset(&C);
    C.esp = SCRATCH + 0x300u;
    WR32(C.esp, 0); WR32(C.esp + 4u, (uint32_t)'A'); WR32(C.esp + 8u, 2u);
    imp_USER32_MapVirtualKeyA(&C);
    check("VK 'A' maps to the character 'A'", C.eax, (uint32_t)'A');

    /* A key that produces NO character must answer 0, not a plausible byte:
       the game stores whatever comes back as the key's printed name. */
    cpu_reset(&C);
    C.esp = SCRATCH + 0x300u;
    WR32(C.esp, 0); WR32(C.esp + 4u, 0x10u); WR32(C.esp + 8u, 2u);
    imp_USER32_MapVirtualKeyA(&C);
    check("VK_SHIFT has no character", C.eax, 0u);

    cpu_reset(&C);
    C.esp = SCRATCH + 0x300u;
    WR32(C.esp, 0); WR32(C.esp + 4u, 0x66u); WR32(C.esp + 8u, 1u);
    imp_USER32_MapVirtualKeyA(&C);
    check("an unassigned scancode maps to nothing", C.eax, 0u);

    /* The device, through the IDirectInput8 vtable. */
    memcpy((void *)(uintptr_t)guid, KBD, 16);
    cpu_reset(&C);
    C.esp = SCRATCH + 0x300u;
    WR32(C.esp, 0);
    WR32(C.esp +  4u, 0);            /* hinst */
    WR32(C.esp +  8u, 0x800u);       /* version */
    WR32(C.esp + 12u, 0);            /* riid */
    WR32(C.esp + 16u, out);          /* ppvOut */
    WR32(C.esp + 20u, 0);            /* punkOuter */
    WR32(out, 0);
    imp_DINPUT8_DirectInput8Create(&C);
    di = RD32(out);
    check("DirectInput8Create yields an object", di != 0u, 1u);

    args[0] = guid; args[1] = out; args[2] = 0;
    hr = com_call(di, 3 /* CreateDevice */, args, 3);
    dev = RD32(out);
    check("CreateDevice(GUID_SysKeyboard) succeeds", hr == 0u && dev != 0u, 1u);

    /* A state read before Acquire must be refused. */
    args[0] = 256; args[1] = state;
    hr = com_call(dev, 9 /* GetDeviceState */, args, 2);
    check("a state read before Acquire is refused", hr, 0x8007000Cu);

    /* And an Acquire before the data format is known. */
    hr = com_call(dev, 7 /* Acquire */, NULL, 0);
    check("Acquire before SetDataFormat is refused", hr, 0x80070057u);

    /* DIDATAFORMAT: dwSize, dwObjSize, dwFlags, dwDataSize, dwNumObjs, rgodf */
    WR32(df +  0u, 24); WR32(df +  4u, 16); WR32(df +  8u, 2);
    WR32(df + 12u, 256); WR32(df + 16u, 256); WR32(df + 20u, 0);
    args[0] = df;
    hr = com_call(dev, 11 /* SetDataFormat */, args, 1);
    check("SetDataFormat is accepted", hr, 0u);

    hr = com_call(dev, 7, NULL, 0);
    check("Acquire then succeeds", hr, 0u);

    memset((void *)(uintptr_t)state, 0xA5, 256);
    args[0] = 256; args[1] = state;
    hr = com_call(dev, 9, args, 2);
    check("GetDeviceState fills the declared size", hr, 0u);
    check("and cleared the buffer it was given",
          *(volatile uint8_t *)(uintptr_t)state, 0u);

    /* A size the caller's own format did not declare is a layout
       disagreement, and filling the smaller of the two would put the fields
       somewhere else. */
    args[0] = 16; args[1] = state;
    hr = com_call(dev, 9, args, 2);
    check("a size the format did not declare is refused", hr, 0x80070057u);

    com_call(dev, 8 /* Unacquire */, NULL, 0);
    guest_free(guid); guest_free(out); guest_free(df); guest_free(state);
}

/*
 * dynamic_cast, against an RTTI graph built here in guest memory.
 *
 * The walker cannot be checked against the game -- there is no oracle for
 * "which base did it find" -- so the graph is constructed with a KNOWN answer
 * and the three outcomes that must differ are all exercised: a cast that
 * succeeds at a non-zero offset (the one a "return inptr unchanged" stub would
 * pass), a cast to a type that is not a base (which must be NULL, not the
 * nearest match), and a cast on a NULL pointer.
 *
 * Layout, MSVC 32-bit:
 *   object   { vftable }              and *(vftable-4) is the locator
 *   locator  { sig, offset, cdOffset, pTypeDescriptor, pClassDescriptor }
 *   chd      { sig, attributes, numBaseClasses, pBaseClassArray }
 *   bcd      { pTypeDescriptor, numContainedBases, mdisp, pdisp, vdisp, attr }
 *   typedesc { pVFTable, spare, name... }
 */
static uint32_t rtti_typedesc(const char *name)
{
    uint32_t t = guest_malloc(8u + (uint32_t)strlen(name) + 1u);
    WR32(t, 0);
    WR32(t + 4u, 0);
    strcpy((char *)(uintptr_t)(t + 8u), name);
    return t;
}

static uint32_t rtti_bcd(uint32_t td, int32_t mdisp)
{
    uint32_t b = guest_malloc(24);
    WR32(b +  0u, td);
    WR32(b +  4u, 0);
    WR32(b +  8u, (uint32_t)mdisp);
    WR32(b + 12u, 0xFFFFFFFFu);          /* pdisp = -1: not a virtual base */
    WR32(b + 16u, 0);
    WR32(b + 20u, 0);
    return b;
}

static void case_dynamic_cast(void)
{
    uint32_t td_derived = rtti_typedesc(".?AVDerived@@");
    uint32_t td_base    = rtti_typedesc(".?AVBase@@");
    uint32_t td_other   = rtti_typedesc(".?AVOther@@");
    uint32_t arr = guest_malloc(8), chd = guest_malloc(16);
    uint32_t col = guest_malloc(20), vft = guest_malloc(16), obj = guest_malloc(8);
    uint32_t args[5];
    CPU C;

    printf("  dynamic_cast (RTTI walk)\n");
    WR32(arr + 0u, rtti_bcd(td_derived, 0));
    WR32(arr + 4u, rtti_bcd(td_base, 8));      /* Base sits 8 bytes in */
    WR32(chd +  0u, 0); WR32(chd + 4u, 0);
    WR32(chd +  8u, 2); WR32(chd + 12u, arr);
    WR32(col +  0u, 0);
    WR32(col +  4u, 0);                        /* this vftable is at offset 0 */
    WR32(col +  8u, 0);                        /* cdOffset */
    WR32(col + 12u, td_derived);
    WR32(col + 16u, chd);
    /* The locator sits at vftable-4, so the object points PAST it. */
    WR32(vft, col);
    WR32(obj, vft + 4u);

    args[0] = obj; args[1] = 0; args[2] = td_derived;
    args[3] = td_base; args[4] = 0;
    cpu_reset(&C);
    C.esp = SCRATCH + 0x500u;
    WR32(C.esp, 0);
    { int i; for (i = 0; i < 5; i++) WR32(C.esp + 4u + (uint32_t)i * 4u, args[i]); }
    imp_MSVCR71___RTDynamicCast(&C);
    check("a base 8 bytes in is found at +8", C.eax, obj + 8u);

    args[3] = td_other;
    cpu_reset(&C);
    C.esp = SCRATCH + 0x500u;
    WR32(C.esp, 0);
    { int i; for (i = 0; i < 5; i++) WR32(C.esp + 4u + (uint32_t)i * 4u, args[i]); }
    imp_MSVCR71___RTDynamicCast(&C);
    check("a type that is not a base gives NULL", C.eax, 0u);

    args[0] = 0; args[3] = td_base;
    cpu_reset(&C);
    C.esp = SCRATCH + 0x500u;
    WR32(C.esp, 0);
    { int i; for (i = 0; i < 5; i++) WR32(C.esp + 4u + (uint32_t)i * 4u, args[i]); }
    imp_MSVCR71___RTDynamicCast(&C);
    check("a NULL pointer casts to NULL", C.eax, 0u);

    /* The cross-module case: a DIFFERENT TypeDescriptor with the same
       decorated name is the same type. Pointer equality alone would fail this,
       and a type shared between the exe and a libIG DLL has one descriptor in
       each -- so this is not a hypothetical. */
    args[0] = obj; args[3] = rtti_typedesc(".?AVBase@@");
    cpu_reset(&C);
    C.esp = SCRATCH + 0x500u;
    WR32(C.esp, 0);
    { int i; for (i = 0; i < 5; i++) WR32(C.esp + 4u + (uint32_t)i * 4u, args[i]); }
    imp_MSVCR71___RTDynamicCast(&C);
    check("a same-named descriptor from another module matches", C.eax, obj + 8u);
}

/*
 * qsort, whose comparator is GUEST code and is handed pointers it dereferences.
 *
 * The check that matters is not "did it sort" -- an insertion sort is easy to
 * get right and was. It is that BOTH pointers the comparator receives are
 * guest-addressable. One of them was a host malloc truncated to 32 bits, which
 * sorts perfectly whenever the comparator only compares the pointers and
 * corrupts everything the moment it dereferences one; the game's scene-graph
 * comparator dereferences, and it faulted at 0xf7832c60.
 *
 * The comparator here is a native callback given a guest address, so the whole
 * path -- x86_guest_call, the argument frame, the return -- is the real one.
 */
static int g_qsort_calls, g_qsort_bad_ptr;

static void qsort_probe(CPU *C)
{
    uint32_t a = RD32(C->esp + 4u), b = RD32(C->esp + 8u), base, size;
    g_qsort_calls++;
    /* VALIDATED BEFORE DEREFERENCING, so a bad pointer is a reported failure
       rather than a SIGSEGV. The real comparator has no such luxury -- it
       just faults, which is how this arrived as a scene-graph crash. */
    if (!guest_heap_contains(a, &base, &size) ||
        !guest_heap_contains(b, &base, &size)) {
        g_qsort_bad_ptr++;
        C->eax = 0;
        C->esp += 4u;
        return;
    }
    /* Ascending, by the dword each pointer points AT -- which is what makes
       this a dereferencing comparator rather than a pointer comparison. */
    C->eax = (uint32_t)(int32_t)((int32_t)RD32(a) - (int32_t)RD32(b));
    C->esp += 4u;                                 /* __cdecl: the return addr */
}

static void case_qsort(void)
{
    static const uint32_t IN[6] = { 5, 3, 9, 1, 4, 1 };
    uint32_t arr = guest_malloc(sizeof IN), cmp, i;
    int sorted = 1;
    CPU C;

    printf("  qsort with a guest comparator\n");
    for (i = 0; i < 6; i++) WR32(arr + i * 4u, IN[i]);
    cmp = x86_native_callback(qsort_probe, "battery", "qsort_probe", NULL);

    cpu_reset(&C);
    C.esp = SCRATCH + 0x600u;
    WR32(C.esp, 0);
    WR32(C.esp +  4u, arr);
    WR32(C.esp +  8u, 6);
    WR32(C.esp + 12u, 4);
    WR32(C.esp + 16u, cmp);
    imp_MSVCR71_qsort(&C);

    for (i = 1; i < 6; i++)
        if (RD32(arr + i * 4u) < RD32(arr + (i - 1) * 4u)) sorted = 0;
    check("the comparator was actually called", g_qsort_calls > 0, 1);
    check("both arguments are guest-addressable", (uint32_t)g_qsort_bad_ptr, 0u);
    check("the array comes back sorted", (uint32_t)sorted, 1u);
    check("and keeps its equal elements", RD32(arr), 1u);
    guest_free(arr);
}

/*
 * FindFirstFileA/FindNextFileA, against the real install.
 *
 * A wildcard matcher is a DISCRIMINATOR, and a discriminator has to be run
 * against BOTH classes before it can be trusted: one that matched everything
 * and one that matched nothing would each look fine from one direction. So
 * this asks for a pattern that MUST hit (the game's own executable is in
 * GAME_PC_DIR) and one that MUST NOT, and checks the count both ways.
 *
 * `*.*` gets its own check because that is the case Windows and fnmatch
 * disagree about: on Windows it means every file, including names with no dot
 * at all, and a matcher that dropped those would hand the asset scanner a
 * silently shorter list.
 */
static uint32_t find_count(const char *pattern, uint32_t data)
{
    CPU C;
    uint32_t h, n = 0;
    uint32_t spec = guest_malloc(512);

    snprintf((char *)(uintptr_t)spec, 512, "%s", pattern);
    cpu_reset(&C);
    C.esp = SCRATCH + 0x700u;
    WR32(C.esp, 0);
    WR32(C.esp + 4u, spec);
    WR32(C.esp + 8u, data);
    imp_KERNEL32_FindFirstFileA(&C);
    h = C.eax;
    if (h == 0xFFFFFFFFu) { guest_free(spec); return 0; }
    do {
        n++;
        cpu_reset(&C);
        C.esp = SCRATCH + 0x700u;
        WR32(C.esp, 0);
        WR32(C.esp + 4u, h);
        WR32(C.esp + 8u, data);
        imp_KERNEL32_FindNextFileA(&C);
    } while (C.eax);
    cpu_reset(&C);
    C.esp = SCRATCH + 0x700u;
    WR32(C.esp, 0);
    WR32(C.esp + 4u, h);
    imp_KERNEL32_FindClose(&C);
    guest_free(spec);
    return n;
}

static void case_find_file(void)
{
    uint32_t data = guest_malloc(320);
    uint32_t hits_exe, hits_none, hits_all;
    const char *name;

    printf("  FindFirstFileA over the install directory\n");
    memset((void *)(uintptr_t)data, 0xEE, 320);          /* poison first */

    hits_exe  = find_count("*.exe", data);
    name = (const char *)(uintptr_t)(data + 44u);
    check("*.exe matched at least one file", hits_exe > 0, 1u);
    check("  and the name is NUL-terminated ASCII",
          (uint32_t)(name[0] > 32 && name[0] < 127 &&
                     memchr(name, 0, 260) != NULL), 1u);
    check("  and the size fields were filled",
          RD32(data + 28u) != 0xEEEEEEEEu && RD32(data + 32u) != 0xEEEEEEEEu, 1u);
    check("  and cAlternateFileName is empty, not poison",
          RD8(data + 304u), 0u);

    hits_none = find_count("*.no-such-extension", data);
    check("a pattern that matches nothing returns nothing", hits_none, 0u);

    hits_all = find_count("*.*", data);
    check("*.* matches at least as much as *.exe", hits_all >= hits_exe, 1u);
    guest_free(data);
}

static int run_battery(void)
{
    printf("battery: recompiled bodies run natively, with real postconditions\n");
    if (pe_map_anon_low(SCRATCH, 0x1000u) != 0) {
        fprintf(stderr, "battery: could not place the scratch object -- ran "
                        "NOTHING\n");
        return 1;
    }
    if (selftest) {
        int before;
        printf("  --selftest: running the battery with every body SKIPPED.\n"
               "  Each check below MUST fail; any that passes is a check that\n"
               "  does not bind.\n");
        skip_body = 1;
        before = fails;
        case_enumerate();
        case_getscreensize();
        case_findmouse();
        case_arkinit();
        case_import_abi();
        case_matrix_multiply();
        skip_body = 0;
        if (fails - before == checks) {
            printf("\n  SELFTEST passed: all %d checks failed with the bodies\n"
                   "  skipped, so a pass below means the bodies did the work.\n"
                   "  The full run has more checks than that, and the extra ones\n"
                   "  are deliberately outside this control: the cross-module\n"
                   "  checks test IMPORT BINDING, which does not depend on any\n"
                   "  body running, so skipping bodies could not falsify them.\n"
                   "  They fail if the slot is unbound, which is their own\n"
                   "  negative.\n\n",
                   checks);
            fails = before;
            checks = 0;
        } else {
            printf("\n  SELFTEST FAILED: only %d of %d checks noticed that the\n"
                   "  bodies never ran. Every result below is unreliable.\n\n",
                   fails - before, checks);
            return 1;
        }
    }
    case_enumerate();
    case_getscreensize();
    case_findmouse();
    case_arkinit();
    case_import_abi();
    case_guest_heap();
    case_setjmp_table();
    case_runtime_module();
    case_dinput();
    case_dynamic_cast();
    case_qsort();
    case_find_file();
    case_matrix_multiply();
    case_cross_module();
    printf("\nbattery: %d of %d check(s) FAILED\n", fails, checks);
    printf("Established: the original image maps at its own base in a 64-bit\n"
           "process, the emitted C runs there natively, image-relative\n"
           "immediates are rebased, guest stack arguments and RET N cleanup are\n"
           "right, and the native import layer gets stdcall and cdecl cleanup\n"
           "right on a known-answer call.\n"
           "NOT established: the game running. Of the 107 imports the bodies\n"
           "reach, 64 are other game modules still to be recompiled and the\n"
           "rest of the Win32 surface is unimplemented -- every one of those\n"
           "aborts by name if reached. rc-native tracks the remainder.\n");
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *dir;
    int window, i, rc, mapped = 0, run, arkprobe, vk;
    int vkselftest, vkpermissive, d3d8, d3d8selftest, d3d8permissive;
    int dialogselftest;
    X2NativeOptions options;
    X86Module *m;
    /* Room for every shipped libIG*.dll plus the exe: the game has 16 of them
       and the recompiled set grows one module at a time (libMovie was the
       ninth). The bound is checked below and reported, so overflowing it stops
       rather than corrupting -- but there is no reason to keep it tight. */
    static PeImage imgs[24];

    g_argv0 = argv[0];
    /* Direct invocation is a supported launch path throughout the docs.  The
       binary therefore loads the project's gitignored .env itself; requiring
       every diagnostic command to remember shell export semantics caused a
       valid install to be reported as absent. Explicit launcher variables win. */
    if (x2_load_project_env(argv[0]) < 0) return 2;
#ifdef X86_NATIVE_REACHED
    /* Also on the ordinary exit path: a run that ends without faulting must
       still say what it reached, or the instrument only ever speaks when
       something else already went wrong. The fault handler _exit()s and so
       calls it directly. */
    atexit(x86_reached_report);
#endif
    /* setjmp/longjmp crosses generated frames now (see crt.c). How many times
       it actually resumed is the difference between the mechanism working and
       the run merely getting further. */
    {
        extern void x86_setjmp_report(void);
        atexit(x86_setjmp_report);
    }
    if ((rc = x2native_options_parse(argc, argv, &options)) != 0) return rc;
    dir = options.install_dir;
    window = options.window;
    if (options.unbounded) guest_clock_set_unbounded(1);
    control_start(options.control);
    selftest = options.selftest;
    run = options.run;
    arkprobe = options.ark_probe;
    vk = options.vk;
    vkselftest = options.vk_selftest;
    vkpermissive = options.vk_permissive;
    d3d8 = options.d3d8;
    d3d8selftest = options.d3d8_selftest;
    d3d8permissive = options.d3d8_permissive;
    dialogselftest = options.dialog_selftest;
    /*
     * The renderer's host half stands alone, so it is checked alone.
     *
     * src/vulkan/gpu_device.c takes no guest state, which means its frame
     * path can be driven with no engine, no ARK and no game install -- and it
     * needs to be, because the game has never reached a frame, so nothing
     * else has ever run that code. Handled before the GAME_PC_DIR check for
     * the same reason: it does not need the install.
     */
    /*
     * The report box, checked with no engine and no game install -- it needs
     * neither, and without this the only thing that ever runs it is a run that
     * has already gone wrong. See src/native/reportbox.c.
     */
    if (dialogselftest) {
        extern int report_box_selftest(void);
        win32_sdl_hide_windows(1);        /* a test must not open a modal */
        return report_box_selftest();
    }
    /* The fault reporter, proved by faulting -- no install, no engine. */
    if (options.probe_selftest) {
        extern int oracle_probe_selftest(void);
        return oracle_probe_selftest();
    }
    if (options.fault_selftest) return fault_selftest();
    if (vkselftest) {
        extern int gpu_device_selftest(void);
        extern int gpu_draw_selftest(void);
        extern int gpu_midframe_clear_selftest(void);
        int r = gpu_device_selftest();
        if (r) return r;
        r = gpu_midframe_clear_selftest();
        if (r && r != 77) return r;
        {
            extern int gpu_cube_texgen_selftest(void);
            extern int gpu_tfactor_selftest(void);
            r = gpu_cube_texgen_selftest();
            if (r && r != 77) return r;
            r = gpu_tfactor_selftest();
            if (r && r != 77) return r;
        }
        /* Presenting a frame and DRAWING into one are different claims. The
           first has been true here since before any geometry worked. */
        return gpu_draw_selftest();
    }
    /*
     * Same reasoning for the host D3D8: its ABI tables, its vtable dispatch
     * and its caps block are host-side facts that need no game to check.
     *
     * It does need guest-addressable memory, because a vtable the guest could
     * dispatch through has to live where the guest could reach it -- so the
     * arena comes up here exactly as it does for a real run. Nothing else of
     * the run is started.
     */
    if (d3d8selftest) {
        if (guest_heap_init(X2_RUNTIME_BASE + 0x01000000u, 0x20000000u) != 0)
            return 1;
        return d3d8_host_selftest();
    }

    if (!dir) dir = getenv("GAME_PC_DIR");
    if (!dir || !*dir) {
        printf("SKIP x2native: no install directory given and GAME_PC_DIR is "
               "unset, so there is nothing to map. NOTHING was checked.\n");
        return 77;
    }

    /* Map every module that was linked in. They are all linked for 0x10000000
       and cannot all be there, so the first one to ask gets its preferred base
       and the rest are relocated -- which is exactly what the loader does in
       the hosted build, and why absolute references are emitted against each
       module's own base rather than a shared one. */
    for (m = x86_modules(); m; m = m->next) {
        char path[4096];
        if (mapped == (int)(sizeof imgs / sizeof imgs[0])) {
            fprintf(stderr, "x2native: more modules than image slots\n");
            return 1;
        }
        snprintf(path, sizeof path, "%s/%s", dir, m->name);
        if (pe_map_at(path, m->preferred, &imgs[mapped]) != 0) return 1;
        *m->base = imgs[mapped].base;
        m->size = imgs[mapped].size;   /* the mapped truth, not a guess */
        printf("mapped %-18s at 0x%08x (%u bytes, %d recompiled bodies)%s\n",
               m->name, imgs[mapped].base, imgs[mapped].size, m->nfns,
               imgs[mapped].base == m->preferred ? "" : "  [relocated]");
        mapped++;
    }
    if (!mapped) {
        fprintf(stderr, "x2native: NO recompiled module is linked in -- this "
                        "binary would check nothing\n");
        return 1;
    }
    g_imgbase = g_imgbase_libIGDisplay;      /* the battery's frame of reference */

    /* Every module is placed, so each override's (module, linked ep) can now
       become the mapped address the dispatcher compares. This must happen
       before any guest code runs: an unresolved table is skipped silently and
       the recompiled bodies answer instead. */
    x86_overrides_resolve();
    if (options.override_selftest) return override_selftest();

    /* Bind imports only once every module is mapped: a slot pointing into a
       module that has not been placed yet would be bound to a stale base. */
    if (poison_init() != 0) return 1;
    if (pe_map_anon_low(DATA_ARENA, DATA_SIZE) != 0) return 1;
    /*
     * The guest's address space above the images, stated in one place:
     *
     *   0x71000000 .. 0x91000000   the heap, 512 MB
     *   0x98000000 .. 0xF0000000   file views (see kernel32.c)
     *
     * It was 128 MB, and that was not enough: the game exhausted it during
     * startup, took its own out-of-memory path, and died calling an
     * uninstalled handler -- a crash with no visible connection to memory.
     * The arena is reserved, not committed, so the size costs address space
     * rather than RAM.
     */
    if (guest_heap_init(X2_RUNTIME_BASE + 0x01000000u, 0x20000000u) != 0)
        return 1;
    atexit(guest_heap_report);
    /* The oracle probe stream. An atexit like every other report here -- and
       like them it may be cut short by the kill that ends every run, which is
       why the stream is flushed as it is written and the live count is in the
       heartbeat. */
    { extern void oracle_probe_arm(void);
      extern void oracle_probe_report(void);
      oracle_probe_arm(); atexit(oracle_probe_report); }
    { extern void dinput_report(void); atexit(dinput_report); }
    { extern void dinput8_install(void), dinput8_report(void);
      dinput8_install(); atexit(dinput8_report); }
    { extern void dsound_install(void), dsound_report(void);
      dsound_install(); atexit(dsound_report); }
    /* SHELL32: the save directory. Installed with the other native system
       modules, because LoadLibraryA may only hand back a handle for a module
       this host actually implements, and that answer comes from the export
       registry. */
    /* NOT atexit: see x2_interrupt_reports, which calls it on every ending. */
    shell32_install();
    advapi32_install(); atexit(advapi32_report);
    {   extern void kernel32_narrowing_report(void);
        atexit(kernel32_narrowing_report); }
    { extern void winmm_report(void); atexit(winmm_report); }
    atexit(guest_thread_report);
    { extern void gdi32_report(void); atexit(gdi32_report); }
    /*
     * Registered here AND called from x2_interrupt_reports, because neither
     * path covers every ending: atexit misses the clean frame-limit stop
     * (which leaves through _exit) and the interrupt reports miss a run that
     * returns normally, like --selftest. Both call the same functions and each
     * prints ONCE -- the guard is in the report itself, so no caller has to
     * know whether another one already ran.
     */
    { extern void crt_rtti_report(void), dinput_device_report(void),
                  dinput_pad_report(void);
      atexit(dinput_device_report); atexit(dinput_pad_report);
      atexit(crt_rtti_report); }
    atexit(x86_native_export_report);
    x86_native_data_arena(DATA_ARENA, DATA_SIZE);
    for (m = x86_modules(); m; m = m->next) {
        int bound = 0, poisoned = 0;
        pe_bind_imports(*m->base, resolve_import, NULL, &bound, &poisoned);
    }
    printf("imports: %d bound to recompiled modules, %d unresolved and poisoned"
           " (using one faults by name)\n", g_nbound, g_npoison);

    /* Before module init, not after: the entry points run ON the guest stack,
       and with none placed their frames land at guest_stack_top - N, i.e. just
       below zero. That is how this first appeared -- a SIGSEGV at 0xfffffff0. */
    if (guest_stack_init() != 0) {
        fprintf(stderr, "x2native: could not place the guest stack\n");
        return 1;
    }
    if (tib_init() != 0) return 1;
    printf("modules: initialising (dependencies first)\n");
    if (modules_init() != 0) {
        fprintf(stderr, "x2native: module initialisation failed -- globals are "
                        "not built, so nothing below would mean anything\n");
        return 1;
    }

#ifdef X2_WITH_SDL
    if (window) {
        SDL_Window *w;
        /* SDL3 returns true on success, where SDL2 returned 0. */
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            fprintf(stderr, "x2native: SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        w = SDL_CreateWindow("x2native", 800, 600, 0);
        if (!w) {
            fprintf(stderr, "x2native: SDL_CreateWindow failed: %s\n",
                    SDL_GetError());
            SDL_Quit();
            return 1;
        }
        printf("SDL: a real window exists; this is where the USER32/DINPUT/D3D8"
               " surface lands.\n");
        SDL_DestroyWindow(w);
        SDL_Quit();
    } else {
        printf("SDL: window skipped (--no-window); the guest's own window "
               "will be created HIDDEN and the renderer goes off-screen\n");
        win32_sdl_hide_windows(1);
        /* A hidden window is not enough on its own: SDL hands back no
           swapchain image for one, so every frame would be refused with "no
           frame is open" while the run went on counting frames. Measured. */
        gpu_device_headless(1, 0, 0);
    }
#else
    (void)window;
    printf("SDL: not compiled in\n");
#endif

    if (d3d8) {
        /*
         * No arming and no waiting: the guest enters this host through an
         * IMPORT, and an import is already bound before the first instruction
         * runs. That is the whole practical difference from --vk, which has to
         * wait for ARK to come up before it can substitute a class.
         */
        if (vk) {
            fprintf(stderr, "x2native: --d3d8 and --vk are two different "
                            "renderers for the same engine. Pick one: --vk "
                            "replaces igVisualContext's vtable, --d3d8 answers "
                            "the DirectX the engine's own vtable calls.\n");
            return 2;
        }
        if (d3d8permissive) {
            d3d8_permissive(1);
            printf("d3d8: PERMISSIVE MODE -- unimplemented D3D8 methods will "
                   "be IGNORED (returning 0) instead of stopping.\n"
                   "  This exists to see what the engine asks for NEXT. "
                   "Anything drawn is missing whatever those methods do; the "
                   "list is printed at exit.\n");
        }
        d3d8_host_enable();
        atexit(d3d8_host_report);
        printf("d3d8: host Direct3D 8 armed on d3d8.dll!Direct3DCreate8\n");
        d3d8_frame_table_install_signal();
        printf("d3d8: press F9 (or send SIGUSR1) to dump every draw of the "
               "next frame -- its screen rectangle, format, lighting and "
               "OBJECT-SPACE EXTENTS, which is what tells a flat mesh from a "
               "flat transform.\n");
        run = 1;
    }

    if (vk) {
        /* Same timing constraint as the probe: ARK registration needs the
           engine's pools, which do not exist until the exe has started. Armed
           on the engine's own first createInstance -- and deliberately on that
           rather than a guessed moment, because it is defined by the engine
           being ready rather than by an ordering we assumed. */
        extern int igvk_context_arm(void);
        if (vkpermissive) {
            /*
             * A STAGING switch, and it is announced because a run made with
             * it is not a run of the renderer -- it is a run of the renderer
             * with an unknown amount missing. What was missing is listed at
             * exit.
             */
            extern void igvk_vtable_permissive(int);
            extern void igvk_vtable_permissive_report(void);
            igvk_vtable_permissive(1);
            atexit(igvk_vtable_permissive_report);
            printf("igVk: PERMISSIVE MODE -- unimplemented renderer slots will "
                   "be IGNORED (returning 0, popping their arguments) instead "
                   "of stopping.\n"
                   "  This exists to drive the engine THROUGH the state calls "
                   "that are not written yet and see whether it reaches the "
                   "frame boundary.\n"
                   "  Anything it draws is missing whatever those slots do. "
                   "The list is printed at exit.\n");
        }
        if (igvk_context_arm()) return 1;
        run = 1;
    }

    if (arkprobe) {
        /* Arm only -- the probe itself runs mid-run, when the engine's pools
           and ARK exist. See igvk_probe.c for why it cannot run here. */
        extern int igvk_ark_probe_arm(void);
        if (igvk_ark_probe_arm()) return 1;
        run = 1;
    }

    if (run) {
        /* Call the program's entry point: WinMainCRTStartup, which brings up
           the CRT and then the engine. Everything the battery checks is
           machinery; this is the thing the machinery is for. */
        X86Module *x = NULL;
        CPU C;
        for (m = x86_modules(); m; m = m->next)
            if (!pe_is_dll(*m->base)) x = m;
        if (!x) {
            fprintf(stderr, "x2native: --run needs an EXE module linked in, "
                            "and none is\n");
            return 1;
        }
        {
            uint32_t entry = *x->base + pe_entry_rva(*x->base);
            const char *nm = x86_native_name_at(entry);
            /* Started here and not earlier: everything before this point is
               host setup that either succeeds or says why, and a heartbeat
               over it would only add lines to a log that is already speaking.
               From the entry point on, the guest owns the thread and silence
               becomes ambiguous. */
            heartbeat_start();
            x86_args_build_check();
            guest_quantum_from_env();
            { extern void x86_hotep_arm(const char *);
              x86_hotep_arm(getenv("X2_HOTEP")); }
            { extern void x86_profiler_start(const char *);
              x86_profiler_start(getenv("X2_PROFILE")); }
            { extern uint32_t g_guest_watch_addr;
              const char *gw = getenv("X2_GUEST_WATCH");
              if (gw && *gw)
                  g_guest_watch_addr = (uint32_t)strtoul(gw, NULL, 0); }
            { extern void x86_write_watch_arm(const char *);
              x86_write_watch_arm(getenv("X2_WRITE_WATCH")); }
            dinput_script_start();
            { extern void dinput_pad_virtual_from_env(void);
              dinput_pad_virtual_from_env(); }
            printf("\nrun: %s entry 0x%08x %s\n", x->name, entry,
                   nm ? nm : "(NO RECOMPILED BODY)");
            if (!nm) return 1;
            cpu_reset(&C);
            C.esp = guest_stack_top - 4u;
            gw32(C.esp, 0xDEADBEEFu);
            /* The main thread is a guest thread too: it holds the global guest
               lock for as long as it is executing guest code, and releases it
               only where threads.c says. Taking it here rather than inside the
               dispatcher keeps it to one acquire for the whole run. */
            guest_lock();
            x86_native_call_at(entry, &C);
            guest_unlock();
            printf("run: returned eax=0x%08x\n", C.eax);
            if (arkprobe) {
                extern int igvk_ark_probe_result(void);
                int prc = igvk_ark_probe_result();
#ifdef X2_DCHECK
                { extern void x86_dcall_report(void); x86_dcall_report(); }
#endif
                if (x86_triggers_report() || prc != 0) {
                    printf("ark-probe: FAILED (rc=%d)\n", prc);
                    for (i = 0; i < mapped; i++) pe_unmap(&imgs[i]);
                    return 1;
                }
            }
        }
        for (i = 0; i < mapped; i++) pe_unmap(&imgs[i]);
        return 0;
    }

    printf("\n");
    rc = run_battery();
    for (i = 0; i < mapped; i++) pe_unmap(&imgs[i]);
    return rc;
}
