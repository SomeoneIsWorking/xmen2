/*
 * The fault report, kept separate from x2native's process composition and
 * from the platform handlers that deliver a fault to it.
 *
 * A fault's address names the memory involved, not the instruction that tried
 * to access it. On Android there is no execinfo backtrace API, so the program
 * counter the handler recovers is the one exact host location available at a
 * crash; host_code_location turns that ASLR address into the offset a
 * symbolizer accepts against the unstripped local binary.
 */
#include "fault_report.h"
#include "x2_log.h"

#include "crash_report.hpp"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "host_code_location.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif
#include "platform_posix.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

const char *fault_name(X2FaultKind kind) {
  switch (kind) {
  case X2_FAULT_SEGV:
    return "SIGSEGV";
  case X2_FAULT_ILL:
    return "SIGILL";
  case X2_FAULT_FPE:
    return "SIGFPE";
  case X2_FAULT_BUS:
    return "SIGBUS";
  case X2_FAULT_TRAP:
    return "SIGTRAP";
  case X2_FAULT_KIND_COUNT:
    break;
  }
  return "signal";
}

static void fault_host_pc_report(uintptr_t pc) {
#if defined(__EMSCRIPTEN__)
  char stack[4096];
  (void)pc;
  emscripten_get_callstack(EM_LOG_C_STACK | EM_LOG_JS_STACK, stack,
                           sizeof stack);
  x2_log_error("[HOST WASM STACK] %s\n", stack);
#else
  x2::diagnostics::HostCodeLocation location;

  if (!pc) {
    x2_log_error("[HOST PC] unavailable for this CPU/context\n");
    return;
  }
  if (x2::diagnostics::host_code_location(pc, &location)) {
    x2_log_error("[HOST PC] %s %s 0x%llx\n", x2::diagnostics::kHostSymbolizer,
                 location.image, (unsigned long long)location.offset);
    return;
  }
  x2_log_error("[HOST PC] 0x%llx (no loaded image holds it)\n",
               (unsigned long long)pc);
#endif
}

/*
 * A fault in the poison region is an unbound import being used. Say which.
 *
 * Every other fatal signal lands here too, and the import analysis below is
 * SIGSEGV's alone: for SIGILL/SIGTRAP `si_addr` is the instruction, for SIGFPE
 * the faulting operation, and reading any of them as an import slot would
 * invent an explanation. What they share is the context under `where:` -- the
 * host PC, guest registers and boundary ring -- which names where the guest
 * was executing.
 */
void fault_report(const X2Fault *fault) {
  uint32_t a;
  const char *mod = NULL, *sym;
  void *const address = (void *)fault->address;

  x2::diagnostics::CrashReport::begin(fault_name(fault->kind), fault->number,
                                      fault->code, fault->address, fault->pc);
  if (!guest_memory_host_address(address, &a))
    a = (uint32_t)fault->address;
  if (fault->kind != X2_FAULT_SEGV) {
    x2_log_error("\n*** %s at %p -- %s\n", fault_name(fault->kind), address,
                 fault->meaning);
    if (fault->kind == X2_FAULT_ILL || fault->kind == X2_FAULT_TRAP)
      x2_log_error(
          "    For a guest body this usually means control reached "
          "something that is not code:\n"
          "    a jump through a stale or wrong function pointer, or a "
          "guest RET onto a corrupted stack.\n"
          "    The guest registers and the ring below say where the run "
          "was; the address is the host address it tried to execute.\n");
    goto where;
  }
  sym = x86_thunk_name(a, &mod);
  if (sym) {
    x2_log_error("\n*** the synthetic address 0x%08x was ACCESSED, not "
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
  sym = x86_poison_name(a, &mod);
  if (sym) {
    x2_log_error("\n*** unbound import used: %s!%s\n"
                 "    The guest read or called import slot 0x%08x, which "
                 "nothing could resolve.\n"
                 "    That module is either not linked into this binary "
                 "or does not export that symbol.\n",
                 mod, sym, a);
    _exit(3);
  }
  x2_log_error("\n*** SIGSEGV at %p (not an import slot) -- %s\n", address,
               fault->meaning);
where:
  fault_host_pc_report(fault->pc);
#if defined(__ANDROID__)
  x2_log_error("[HOST STACK] unavailable on Android (no execinfo API)\n");
#endif
  x86_regs_dump();
  x86_diag_dump();
  _exit(3);
}
