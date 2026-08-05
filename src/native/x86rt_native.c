/*
 * The shared native runtime: dispatch across every linked recompiled module.
 * See x86rt_native.h for why dispatch keys on the mapped address rather than
 * the guest entry point.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>

static X86Module *g_head;

static int thunk_call(uint32_t addr, CPU *C);

/* Not used by the shared runtime itself, but the emitted bodies of a
   single-module build still reference the plain symbol. */
uint32_t g_imgbase = 0x10000000U;
uint32_t g_image_lo, g_image_hi;
int x86_allow_fallback;

void x86_module_register(X86Module *m)
{
    m->next = g_head;
    g_head = m;
}

X86Module *x86_modules(void) { return g_head; }

X86Module *x86_module_for(uint32_t addr)
{
    X86Module *m;
    for (m = g_head; m; m = m->next) {
        uint32_t b = *m->base;
        /* size 0 means the host never mapped this module. Saying so beats
           returning NULL, which reads as "that address is host memory". */
        if (b && !m->size) {
            fprintf(stderr, "x86_module_for: %s has a base but no size -- the "
                            "host mapped it and did not record how big it is, "
                            "so every lookup into it will miss\n", m->name);
            abort();
        }
        if (b && addr >= b && addr - b < m->size) return m;
    }
    return NULL;
}

/* Linear search per module. 5769 + 521 entries is small enough that this has
   never shown up in a profile, and a sorted table would have to be built at
   registration -- worth doing when it measurably matters, not before. */
static const X86Fn *find(X86Module *m, uint32_t addr)
{
    uint32_t ep = m->preferred + (addr - *m->base);
    int i;
    for (i = 0; i < m->nfns; i++)
        if (m->fns[i].ep == ep) return &m->fns[i];
    return NULL;
}

int x86_native_call_at(uint32_t addr, CPU *C)
{
    X86Module *m;
    if (thunk_call(addr, C)) return 1;
    m = x86_module_for(addr);
    const X86Fn *f = m ? find(m, addr) : NULL;
    if (!f) return 0;
    f->fn(C);
    return 1;
}

const char *x86_native_name_at(uint32_t addr)
{
    X86Module *m = x86_module_for(addr);
    const X86Fn *f = m ? find(m, addr) : NULL;
    return f ? f->name : NULL;
}

/* ---- native import thunks ---------------------------------------------
 *
 * Some imports are implemented natively but are not another recompiled module,
 * so binding cannot point their IAT slot at a guest body. Most of the time
 * that is harmless -- the emitted code calls the NAMED stub and never reads
 * the slot -- but the exe's CRT startup takes GetModuleHandleA's address from
 * the IAT and calls through it, which reaches no stub at all.
 *
 * So each such slot gets a synthetic address in a range this dispatcher owns,
 * and a call to one runs the stub. The range is deliberately NOT mapped: it is
 * never executed as code and never dereferenced, so leaving it unmapped means
 * a stray READ of one faults instead of returning a plausible word.
 *
 * The test for "implemented natively" is the one thing here that must not be
 * assumed: it compares the stub's address against the aborting default. A
 * module whose import is still the weak stub gets no thunk, and stays
 * poisoned, so nothing is silently promoted to "working".
 */
#define THUNK_BASE 0x000C0000u
#define THUNK_MAX  1024

static struct { void (*stub)(CPU *); const char *mod, *sym; } g_thunk[THUNK_MAX];
static int g_nthunk;

/*
 * A thunk is created whether or not the import is actually implemented, because
 * the generated stubs are weak symbols and an implemented one cannot be told
 * from an unimplemented one by address.
 *
 * That is safe only because of the cycle-break in x86_import_call below. An
 * IMPLEMENTED stub does the work and never looks at its slot. An UNIMPLEMENTED
 * one dispatches through its slot -- which now holds this thunk, which calls
 * the stub, which dispatches through the slot... The first version of this
 * recursed until the stack died, and the comment here claimed it would "report
 * by name and stop". It did not. Wanting a design to be safe is not the same as
 * it being safe, so the break is explicit and tested rather than argued.
 */
uint32_t x86_native_thunk(const char *mod, const char *sym)
{
    X86Module *m;
    int i;
    for (m = g_head; m; m = m->next) {
        for (i = 0; i < m->nimports; i++) {
            const X86Import *im = &m->imports[i];
            if (strcasecmp(im->mod, mod) != 0 || strcmp(im->sym, sym) != 0)
                continue;
            if (g_nthunk == THUNK_MAX) return 0;
            g_thunk[g_nthunk].stub = im->stub;
            g_thunk[g_nthunk].mod = im->mod;
            g_thunk[g_nthunk].sym = im->sym;
            g_nthunk++;
            return THUNK_BASE + (uint32_t)(g_nthunk - 1) * 16u;
        }
    }
    return 0;
}

/* Which import a thunk address belongs to, for diagnostics. A thunk that is
   DEREFERENCED rather than called means the import is data, not a function --
   and a thunk cannot serve data, so that import needs a real value. */
const char *x86_thunk_name(uint32_t addr, const char **mod)
{
    uint32_t i;
    if (addr < THUNK_BASE || addr >= THUNK_BASE + (uint32_t)THUNK_MAX * 16u)
        return NULL;
    i = (addr - THUNK_BASE) / 16u;
    if ((int)i >= g_nthunk) return NULL;
    *mod = g_thunk[i].mod;
    return g_thunk[i].sym;
}

static int thunk_call(uint32_t addr, CPU *C)
{
    uint32_t i;
    if (addr < THUNK_BASE || addr >= THUNK_BASE + (uint32_t)THUNK_MAX * 16u)
        return 0;
    i = (addr - THUNK_BASE) / 16u;
    if ((int)i >= g_nthunk || !g_thunk[i].stub) return 0;
    g_thunk[i].stub(C);
    return 1;
}

/* ---- the abort paths ---------------------------------------------------
 *
 * Each of these names what is missing and stops. None of them may return a
 * plausible value: the native build has no original image mapped alongside it
 * and no Windows loader resolved anything, so there is nothing honest to fall
 * back TO. A recompilation that quietly ran something else would not be one.
 */
/*
 * An address may be a poisoned import slot rather than code. The host owns
 * that table, so it supplies this; the weak default keeps the runtime usable
 * on its own. Without it, a poisoned slot reached by DISPATCH (a `call [iat]`
 * the recompiler turned into an indirect call rather than a named stub) is
 * reported as "no registered module", which reads as a linking problem rather
 * than as the unimplemented import it is -- measured, on the exe's first run.
 */
__attribute__((weak))
const char *x86_poison_name(uint32_t addr, const char **mod)
{
    (void)addr; (void)mod;
    return NULL;
}

static void where(uint32_t addr)
{
    X86Module *m = x86_module_for(addr);
    const char *mod = NULL, *sym = x86_poison_name(addr, &mod);
    if (sym) {
        fprintf(stderr, "  that address is an UNBOUND IMPORT: %s!%s\n"
                        "  it was reached as a call target, so something took "
                        "its address from the IAT\n", mod, sym);
        return;
    }
    if (m)
        fprintf(stderr, "  address is in %s (guest 0x%08x)\n",
                m->name, m->preferred + (addr - *m->base));
    else
        fprintf(stderr, "  address is in NO registered module -- either it is "
                        "host memory or a module was never linked in\n");
}

void x86_dispatch(CPU *C, uint32_t target)
{
    X86Module *m;
    if (x86_native_call_at(target, C)) return;
    fprintf(stderr, "x86_dispatch: no recompiled body at 0x%08x\n", target);
    where(target);
    /* If it is inside a module, it is a function static analysis missed --
       exactly what the constructor-table report describes, so it is printed in
       the SAME shape and tools/native_discover.sh seeds it without needing to
       know that an indirect call target is a different kind of gap. */
    m = x86_module_for(target);
    if (m) {
        fprintf(stderr, "\n*** dispatch target with no recompiled body.\n"
                        "    Reached as an indirect call, so nothing in the "
                        "database references it as code.\n");
        fprintf(stderr, "    %-18s 0x%08x\n", m->name,
                m->preferred + (target - *m->base));
        fprintf(stderr, "*** 1 of 1 dispatch target is missing a body\n");
    }
    abort();
}

void x86_return_to(CPU *C, uint32_t target)
{
    if (x86_native_call_at(target, C)) return;
    fprintf(stderr, "x86_return_to: 0x%08x is not a function entry -- a RET "
                    "redirected into the middle of a function\n", target);
    where(target);
    abort();
}

void x86_call_unknown(CPU *C, uint32_t target)
{
    (void)C;
    fprintf(stderr, "x86_call_unknown: 0x%08x has no identified function\n",
            target);
    where(target);
    abort();
}

void x86_missing_import(const char *mod, const char *sym)
{
    fprintf(stderr, "x86_missing_import: %s!%s is not implemented natively.\n"
                    "  This is the native import surface -- the work that "
                    "replaces Wine.\n", mod, sym);
    abort();
}

void x86_untranslated(uint32_t ep, const char *name, const char *reason)
{
    fprintf(stderr, "x86_untranslated: reached 0x%08x %s -- blocked by: %s\n",
            ep, name, reason);
    abort();
}

void x86_note_fallback(uint32_t target)
{
    fprintf(stderr, "x86_note_fallback: 0x%08x -- the native build has no "
                    "original image to fall back to\n", target);
    abort();
}

void x86_fallback_report(void) { }

void x86_guest_addr_of(uint32_t addr, const char **mod, uint32_t *guest)
{
    X86Module *m = x86_module_for(addr);
    if (!m) { *mod = NULL; *guest = addr; return; }
    *mod = m->name;
    *guest = m->preferred + (addr - *m->base);
}

void x86_int3(uint32_t addr)
{
    fprintf(stderr, "x86_int3: execution reached the compiler's unreachable "
                    "trap at guest 0x%08x.\n"
                    "  MSVC emits INT3 after a call it proved never returns, "
                    "so a noreturn function returned.\n", addr);
    where(addr);
    abort();
}

void x87_fault(const char *what)
{
    fprintf(stderr, "x87_fault: %s\n", what);
    abort();
}

/*
 * Call an import through its IAT slot.
 *
 * The slot is bound at startup by the host, the way a loader would bind it, so
 * a call into another recompiled module lands on that module's body at its
 * mapped address. The module and symbol are carried along only for the failure
 * case: an unbound slot holds a poison address, and reporting "libIGCore.dll!
 * ?createInstance@..." is worth far more than reporting 0x00090120.
 */
int x86_is_thunk(uint32_t addr)
{
    return addr >= THUNK_BASE && addr < THUNK_BASE + (uint32_t)THUNK_MAX * 16u;
}

void x86_import_call(CPU *C, uint32_t slot_va, const char *mod, const char *sym)
{
    uint32_t target = *(volatile uint32_t *)(uintptr_t)slot_va;
    /* The cycle break. Reaching here means the generated stub is running,
       which only happens when nothing implements this import natively -- so a
       slot pointing back at the thunk that calls this very stub is not a
       target, it is the loop. Report it instead of taking it. */
    if (x86_is_thunk(target)) x86_missing_import(mod, sym);
    if (x86_native_call_at(target, C)) return;
    fprintf(stderr, "x86_import_call: %s!%s\n"
                    "  slot 0x%08x holds 0x%08x, which is not a recompiled "
                    "body.\n", mod, sym, slot_va, target);
    x86_missing_import(mod, sym);
}

/*
 * Call a guest function FROM host code.
 *
 * A recompiled body is entered with its return address already on the guest
 * stack -- every emitted call site pushes one -- and its RET pops it. Host
 * code that dispatches without pushing one therefore leaks 4 bytes of guest
 * stack per call, upward, and the damage is silent until ESP walks off the
 * top: measured as a SIGSEGV 64 bytes above the stack top after 51 static
 * constructors, which reads as stack corruption rather than a missing push.
 *
 * So the convention lives here once instead of at each call site.
 */
void x86_guest_call(CPU *C, uint32_t target)
{
    C->esp -= 4;
    *(volatile uint32_t *)(uintptr_t)C->esp = 0xDEADBEEFu;   /* popped by RET */
    x86_dispatch(C, target);
}
