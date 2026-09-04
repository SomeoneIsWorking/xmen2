#include "x86_dispatch_report.h"
#include "x2_log.h"

#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

void x86_report_where(uint32_t addr) {
  X86Module *m = x86_module_for(addr);
  const char *mod = NULL, *sym = x86_poison_name(addr, &mod);
  if (sym) {
    x2_log_error("  that address is an UNBOUND IMPORT: %s!%s\n"
                 "  it was reached as a call target, so something took "
                 "its address from the IAT\n",
                 mod, sym);
    return;
  }
  if (m)
    x2_log_error("  mapped 0x%08x is in %s (guest 0x%08x)\n", addr, m->name,
                 m->preferred + (addr - *m->base));
  else
    x2_log_error("  address is in NO registered module -- either it is "
                 "host memory or a module was never linked in\n");
}

void x86_report_missing_body(CPU *C, uint32_t target) {
  X86Module *m;
  x2_log_error("x86_dispatch: no executable guest body at 0x%08x\n", target);
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
    uint32_t ra = RD32(C->reg[kX86pEsp]);
    const char *nm = x86_native_name_at(ra);
    X86Module *rm = x86_module_for(ra);
    if (nm)
      x2_log_error("  dispatched from 0x%08x -- %s\n", ra, nm);
    else if (rm)
      x2_log_error("  dispatched from 0x%08x, inside %s (guest "
                   "0x%08x) but not at a body this host can name\n",
                   ra, rm->name, rm->preferred + (ra - *rm->base));
    else
      x2_log_error("  the return address on the guest stack is "
                   "0x%08x, which is in no module either -- so the "
                   "STACK is suspect, not just the target\n",
                   ra);
  }
  /* The engine has already produced its exact refusal. Add only mapped-image
     identity and call-site context here; do not guess a function boundary. */
  x86_diag_dump();
  m = x86_module_for(target);
  if (m) {
    x2_log_error("\n*** x86port declined a mapped guest dispatch target.\n");
    x2_log_error("    %-18s 0x%08x\n", m->name,
                 m->preferred + (target - *m->base));
    x2_log_error("*** dispatch cannot continue\n");
  }
}
