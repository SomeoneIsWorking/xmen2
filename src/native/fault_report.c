#include "x2_log.h"
/*
 * Fatal-signal reporting, kept separate from x2native's process composition.
 *
 * A signal's si_addr names the memory address involved, not the instruction
 * that tried to access it.  On Android there is no execinfo backtrace API, so
 * the program counter in ucontext is the one exact host location available at
 * a crash.  dladdr turns that ASLR address into the offset accepted by
 * llvm-addr2line against the unstripped local libmain.so.
 */
#include "fault_report.h"

#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#if !defined(__ANDROID__)
#include <execinfo.h>
#endif
#include <dlfcn.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>

static const char *fault_meaning(int sig, int code) {
  switch (sig) {
  case SIGSEGV:
    return code == SEGV_MAPERR   ? "address not mapped"
           : code == SEGV_ACCERR ? "no permission for that access"
                                 : "a memory access fault";
  case SIGILL:
    return code == ILL_ILLOPC   ? "illegal OPCODE -- the instruction at this "
                                  "address is not an instruction"
           : code == ILL_ILLOPN ? "illegal operand"
           : code == ILL_ILLADR ? "illegal addressing mode"
           : code == ILL_PRVOPC ? "privileged opcode"
           : code == ILL_ILLTRP ? "illegal trap"
                                : "an illegal instruction";
  case SIGFPE:
    return code == FPE_INTDIV   ? "integer divide by zero"
           : code == FPE_INTOVF ? "integer overflow"
           : code == FPE_FLTDIV ? "floating-point divide by zero"
           : code == FPE_FLTINV ? "invalid floating-point operation"
                                : "an arithmetic fault";
  case SIGBUS:
    return code == BUS_ADRALN   ? "misaligned address"
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

const char *fault_name(int sig) {
  switch (sig) {
  case SIGSEGV:
    return "SIGSEGV";
  case SIGILL:
    return "SIGILL";
  case SIGFPE:
    return "SIGFPE";
  case SIGBUS:
    return "SIGBUS";
  case SIGTRAP:
    return "SIGTRAP";
  default:
    return "signal";
  }
}

static uintptr_t fault_context_pc(const void *context) {
  const ucontext_t *uc = context;
  if (!uc)
    return 0;
#if defined(__x86_64__) && defined(REG_RIP)
  return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__) && defined(REG_EIP)
  return (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
#elif defined(__aarch64__)
  return (uintptr_t)uc->uc_mcontext.pc;
#else
  return 0;
#endif
}

static void fault_host_pc_report(const void *context) {
  uintptr_t pc = fault_context_pc(context);
  Dl_info info;

  if (!pc) {
    x2_log_error("[HOST PC] unavailable for this CPU/context\n");
    return;
  }
  if (dladdr((const void *)pc, &info) && info.dli_fbase && info.dli_fname) {
    x2_log_error("[HOST PC] addr2line -fCe %s 0x%llx\n", info.dli_fname,
                 (unsigned long long)(pc - (uintptr_t)info.dli_fbase));
    return;
  }
  x2_log_error("[HOST PC] 0x%llx (dladdr could not resolve its image)\n",
               (unsigned long long)pc);
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
void fault_report(int sig, siginfo_t *si, void *uc) {
  uint32_t a;
  const char *mod = NULL, *sym;

  if (!guest_memory_host_address(si->si_addr, &a))
    a = (uint32_t)(uintptr_t)si->si_addr;
  if (sig != SIGSEGV) {
    x2_log_error("\n*** %s at %p -- %s\n", fault_name(sig), si->si_addr,
                 fault_meaning(sig, si->si_code));
    if (sig == SIGILL || sig == SIGTRAP)
      x2_log_error(
          "    For a guest body this usually means control reached "
          "something that is not code:\n"
          "    a jump through a stale or wrong function pointer, or a "
          "guest RET onto a corrupted stack.\n"
          "    The guest registers and the ring below say where the run "
          "was; si_addr is the host address it tried to execute.\n");
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
  x2_log_error("\n*** SIGSEGV at %p (not an import slot) -- %s\n", si->si_addr,
               fault_meaning(sig, si->si_code));
where:
  fault_host_pc_report(uc);
#if defined(__ANDROID__)
  x2_log_error("[HOST STACK] unavailable on Android (no execinfo API)\n");
#else
  {
    void *frames[32];
    int count = backtrace(frames, (int)(sizeof frames / sizeof frames[0]));
    x2_log_error("[HOST STACK] %d frame(s):\n", count);
    backtrace_symbols_fd(frames, count, STDERR_FILENO);
  }
#endif
  x86_regs_dump();
  x86_diag_dump();
  _exit(3);
}
