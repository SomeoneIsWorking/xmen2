/*
 * The shared native runtime: dispatch across every linked recompiled module.
 * See x86rt_native.h for why dispatch keys on the mapped address rather than
 * the guest entry point.
 */
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>

static X86Module *g_head;

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
    X86Module *m = x86_module_for(addr);
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

/* ---- the abort paths ---------------------------------------------------
 *
 * Each of these names what is missing and stops. None of them may return a
 * plausible value: the native build has no original image mapped alongside it
 * and no Windows loader resolved anything, so there is nothing honest to fall
 * back TO. A recompilation that quietly ran something else would not be one.
 */
static void where(uint32_t addr)
{
    X86Module *m = x86_module_for(addr);
    if (m)
        fprintf(stderr, "  address is in %s (guest 0x%08x)\n",
                m->name, m->preferred + (addr - *m->base));
    else
        fprintf(stderr, "  address is in NO registered module -- either it is "
                        "host memory or a module was never linked in\n");
}

void x86_dispatch(CPU *C, uint32_t target)
{
    if (x86_native_call_at(target, C)) return;
    fprintf(stderr, "x86_dispatch: no recompiled body at 0x%08x\n", target);
    where(target);
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
void x86_import_call(CPU *C, uint32_t slot_va, const char *mod, const char *sym)
{
    uint32_t target = *(volatile uint32_t *)(uintptr_t)slot_va;
    if (x86_native_call_at(target, C)) return;
    fprintf(stderr, "x86_import_call: %s!%s\n"
                    "  slot 0x%08x holds 0x%08x, which is not a recompiled "
                    "body.\n", mod, sym, slot_va, target);
    x86_missing_import(mod, sym);
}
