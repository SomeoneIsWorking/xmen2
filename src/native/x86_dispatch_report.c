#include "x86_dispatch_report.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

void x86_report_where(uint32_t addr)
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
        fprintf(stderr, "  mapped 0x%08x is in %s (guest 0x%08x)\n",
                addr, m->name, m->preferred + (addr - *m->base));
    else
        fprintf(stderr, "  address is in NO registered module -- either it is "
                        "host memory or a module was never linked in\n");
}

void x86_report_missing_body(CPU *C, uint32_t target)
{
    X86Module *m;
    fprintf(stderr, "x86_dispatch: no recompiled body at 0x%08x\n", target);
    x86_report_where(target);
    /*
     * WHO dispatched there. Every emitted indirect call pushes its own return
     * address before dispatching, so the word at ESP names the call site --
     * and without it this report says only that a bad target was reached,
     * which is the one thing the reader already knows. The value is checked
     * against the module list rather than trusted: if the stack is the thing
     * that is wrong, the return address is wrong too, and saying so is part of
     * the answer.
     */
    {
        uint32_t ra = RD32(C->esp);
        const char *nm = x86_native_name_at(ra);
        X86Module *rm = x86_module_for(ra);
        if (nm)
            fprintf(stderr, "  dispatched from 0x%08x -- %s\n", ra, nm);
        else if (rm)
            fprintf(stderr, "  dispatched from 0x%08x, inside %s (guest "
                            "0x%08x) but not at a body this host can name\n",
                    ra, rm->name, rm->preferred + (ra - *rm->base));
        else
            fprintf(stderr, "  the return address on the guest stack is "
                            "0x%08x, which is in no module either -- so the "
                            "STACK is suspect, not just the target\n", ra);
    }
    /* If it is inside a module, it is a function static analysis missed --
       exactly what the constructor-table report describes, so it is printed in
       the SAME shape and tools/native_discover.py seeds it without needing to
       know that an indirect call target is a different kind of gap. */
    x86_diag_dump();
    m = x86_module_for(target);
    if (m) {
        fprintf(stderr, "\n*** dispatch target with no recompiled body.\n"
                        "    Reached as an indirect call, so nothing in the "
                        "database references it as code.\n");
        fprintf(stderr, "    %-18s 0x%08x\n", m->name,
                m->preferred + (target - *m->base));
        fprintf(stderr, "*** 1 of 1 dispatch target is missing a body\n");
    }
}
