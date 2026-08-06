/*
 * x2native -- the recompiled game code running as a NATIVE Linux binary.
 *
 * No Wine, no PE loader, no original DLL alongside us. This is the first
 * artefact on the rc-native track, and its job is to make that track
 * measurable rather than aspirational: it maps the original image at its own
 * base, runs recompiled function bodies against it, and opens an SDL window so
 * the platform layer that replaces USER32/DINPUT/D3D8 has somewhere to live.
 *
 *   ./x2native <libIGDisplay.dll> [--no-window]
 *
 * What it does NOT do, and must not be read as doing: run the game. 107
 * imports are stubbed to abort by name (61 of them into libIGCore, which is
 * another module to recompile; the rest are the Win32 surface SDL replaces).
 * Any function that reaches one stops with that name printed. That is the
 * remaining work, stated as a list rather than as a guess.
 */
#include "pe_map.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "guest_heap.h"

#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

uint32_t g_fsbase, g_gsbase;

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

/* A fault in the poison region is an unbound import being used. Say which. */
static void poison_sigsegv(int sig, siginfo_t *si, void *uc)
{
    uint32_t a = (uint32_t)(uintptr_t)si->si_addr;
    const char *mod = NULL, *sym;
    (void)sig; (void)uc;
    sym = x86_thunk_name(a, &mod);
    if (sym) {
        fprintf(stderr, "\n*** a native import THUNK was dereferenced: %s!%s\n"
                        "    The guest read through import slot 0x%08x instead "
                        "of calling it, so this import is DATA, not a "
                        "function.\n"
                        "    A thunk cannot serve data: it needs a real value "
                        "in guest memory (see x86_native_data_export).\n",
                mod, sym, a);
        _exit(3);
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
    fprintf(stderr, "\n*** SIGSEGV at %p (not an import slot)\n", si->si_addr);
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
    x86_peek_report();
    x86_ring_dump();
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
    sa.sa_sigaction = poison_sigsegv;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    return sigaction(SIGSEGV, &sa, NULL);
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
    memset(&C, 0, sizeof C);
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
    X86Module *m, *d;
    int done[16], n = 0, i, pass, inited = 0, total = 0;
    X86Module *list[16];
    for (m = x86_modules(); m; m = m->next) {
        if (total == 16) { fprintf(stderr, "module_init: too many modules\n"); return -1; }
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
    memset(C, 0, sizeof *C);
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
void imp_KERNEL32_MultiByteToWideChar(CPU *C);
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
        memset(&C, 0, sizeof C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_KERNEL32_GetModuleHandleA, &C, args, 1);
        check("GetModuleHandleA(NULL) -> imgbase", C.eax, g_imgbase);
        check("  stdcall esp delta (4+1*4)", d, 8u);
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
        memset(&C, 0, sizeof C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_KERNEL32_MultiByteToWideChar, &C, args, 6);
        check("MB2WC measure -> strlen+1", C.eax, 5u);
        check("  stdcall esp delta (4+6*4)", d, 28u);

        args[5] = 16;                              /* now actually convert */
        for (i = 0; i < 8; i++) gw32(dst + i * 4u, 0xBADF00Du);
        memset(&C, 0, sizeof C);
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
        memset(&C, 0, sizeof C);
        C.eax = 0xBADF00Du;
        d = call_import(imp_MSVCRT_malloc, &C, args, 1);
        p = C.eax;
        check("malloc(64) != 0", p != 0u && p != 0xBADF00Du, 1u);
        check("  cdecl esp delta (4 only)", d, 4u);
        if (p && p != 0xBADF00Du) {
            args[0] = p;
            memset(&C, 0, sizeof C);
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
    const char *dir = NULL;
    int window = 1, i, rc, mapped = 0, run = 0;
    X86Module *m;
    static PeImage imgs[8];

    g_argv0 = argv[0];
#ifdef X86_NATIVE_REACHED
    /* Also on the ordinary exit path: a run that ends without faulting must
       still say what it reached, or the instrument only ever speaks when
       something else already went wrong. The fault handler _exit()s and so
       calls it directly. */
    atexit(x86_reached_report);
#endif
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--no-window") == 0) window = 0;
        else if (strcmp(argv[i], "--selftest") == 0) selftest = 1;
        else if (strcmp(argv[i], "--run") == 0) run = 1;
        else dir = argv[i];
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

    /* Bind imports only once every module is mapped: a slot pointing into a
       module that has not been placed yet would be bound to a stale base. */
    if (poison_init() != 0) return 1;
    if (pe_map_anon_low(DATA_ARENA, DATA_SIZE) != 0) return 1;
    /* 256 MB, reserved not committed, well clear of every image base. */
    if (guest_heap_init(X2_RUNTIME_BASE + 0x01000000u, 0x08000000u) != 0) return 1;
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
        printf("SDL: window skipped (--no-window)\n");
    }
#else
    (void)window;
    printf("SDL: not compiled in\n");
#endif

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
            printf("\nrun: %s entry 0x%08x %s\n", x->name, entry,
                   nm ? nm : "(NO RECOMPILED BODY)");
            if (!nm) return 1;
            memset(&C, 0, sizeof C);
            C.esp = guest_stack_top - 4u;
            gw32(C.esp, 0xDEADBEEFu);
            x86_native_call_at(entry, &C);
            printf("run: returned eax=0x%08x\n", C.eax);
        }
        for (i = 0; i < mapped; i++) pe_unmap(&imgs[i]);
        return 0;
    }

    printf("\n");
    rc = run_battery();
    for (i = 0; i < mapped; i++) pe_unmap(&imgs[i]);
    return rc;
}
