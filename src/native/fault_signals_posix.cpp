/*
 * Darwin's <ucontext.h> refuses to declare the (deprecated but still
 * present) ucontext_t API unless _XOPEN_SOURCE is defined first; every other
 * platform this file targets accepts the include unconditionally. Setting
 * _XOPEN_SOURCE alone then hides Darwin-only extensions, so _DARWIN_C_SOURCE
 * has to come back on top of it.
 */
#if defined(__APPLE__) && !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE
#endif

/* fault_signals_posix.cpp -- fatal signals and SIGTERM/SIGINT delivered to
 * the fault reporter. */
#include "fault_platform.hpp"
#include "heartbeat.h"
#include "x2_log.h"
#include "x86rt_native.h"

#include "platform_posix.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#if !defined(__EMSCRIPTEN__)
#include <ucontext.h>
#endif

namespace {

constexpr int kFatalSignals[] = {SIGSEGV, SIGILL, SIGFPE, SIGBUS, SIGTRAP};

X2FaultKind kind_of(int sig) {
  switch (sig) {
  case SIGILL:
    return X2_FAULT_ILL;
  case SIGFPE:
    return X2_FAULT_FPE;
  case SIGBUS:
    return X2_FAULT_BUS;
  case SIGTRAP:
    return X2_FAULT_TRAP;
  default:
    return X2_FAULT_SEGV;
  }
}

int signal_of(X2FaultKind kind) {
  switch (kind) {
  case X2_FAULT_ILL:
    return SIGILL;
  case X2_FAULT_FPE:
    return SIGFPE;
  case X2_FAULT_BUS:
    return SIGBUS;
  case X2_FAULT_TRAP:
    return SIGTRAP;
  default:
    return SIGSEGV;
  }
}

const char *fault_meaning(int sig, int code) {
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

uintptr_t context_pc(const void *context) {
#if defined(__EMSCRIPTEN__)
  (void)context;
  return 0;
#else
  const ucontext_t *uc = static_cast<const ucontext_t *>(context);
  if (!uc)
    return 0;
#if defined(__APPLE__) && defined(__aarch64__)
  return (uintptr_t)uc->uc_mcontext->__ss.__pc;
#elif defined(__APPLE__) && defined(__x86_64__)
  return (uintptr_t)uc->uc_mcontext->__ss.__rip;
#elif defined(__x86_64__) && defined(REG_RIP)
  return (uintptr_t)uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__i386__) && defined(REG_EIP)
  return (uintptr_t)uc->uc_mcontext.gregs[REG_EIP];
#elif defined(__aarch64__)
  return (uintptr_t)uc->uc_mcontext.pc;
#else
  return 0;
#endif
#endif
}

void on_fatal_signal(int sig, siginfo_t *si, void *uc) {
  const X2Fault fault = {
      kind_of(sig), fault_meaning(sig, si->si_code), sig,
      si->si_code,  (uintptr_t)si->si_addr,          context_pc(uc)};
  fault_report(&fault);
}

constexpr char kInterruptedMessage[] =
    "\n*** x2native was INTERRUPTED -- it did not stop on its own.\n"
    "    Nothing below is a failure the run reported; this is where it\n"
    "    HAPPENED TO BE. A run that has to be killed is usually spinning:\n"
    "    read the ring below for a repeating pair of bodies.\n"
    "    (The ring is best-effort from a signal handler and may be cut\n"
    "    short; everything above this line is complete.)\n";

/*
 * A run that has to be KILLED, reported.
 *
 * It is the one failure mode with nothing to read afterwards: a crash names a
 * body, an abort names a symbol, and a run that never returns leaves a log
 * that stops mid-sentence.
 *
 * ASYNC-SIGNAL-SAFE, the hard way: fprintf here deadlocks whenever the
 * interrupted code holds the stdio lock. So the message goes out with
 * write(2), and the ring dump (stdio) is BEST EFFORT behind an alarm: if it
 * deadlocks or takes too long, SIGALRM ends the process.
 */
void interrupted(int sig) {
  ssize_t ignored;
  (void)sig;
  /* Back to the default first: a second signal must be able to kill this. */
  signal(SIGTERM, SIG_DFL);
  signal(SIGINT, SIG_DFL);
  ignored = write(2, kInterruptedMessage, sizeof kInterruptedMessage - 1);
  (void)ignored;
  /* The reports are stdio, so the heartbeat thread runs them from ordinary
     context; the alarm is the backstop if it never gets there. */
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

/* On an alternate stack, so a fault caused by the guest stack running out --
   or by runaway recursion in the runtime itself -- can still be reported. */
char g_altstack[65536]; /* >= SIGSTKSZ on every target here */

} // namespace

namespace x2::fault {

bool install_fatal_handlers() {
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_sigaction = on_fatal_signal;
  sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
  bool installed = true;
  for (const int sig : kFatalSignals) {
    if (sigaction(sig, &sa, NULL) != 0) {
      x2_log_error("x2native: could not install the fault reporter for %s; "
                   "a fault of that kind will die silently\n",
                   fault_name(kind_of(sig)));
      installed = false;
    }
  }
  return installed;
}

bool install_handlers() {
  stack_t ss;
  ss.ss_sp = g_altstack;
  ss.ss_size = sizeof g_altstack;
  ss.ss_flags = 0;
  if (sigaltstack(&ss, NULL) != 0)
    x2_log_error("x2native: no alternate signal stack; a stack "
                 "overflow will die silently\n");
  const bool installed = install_fatal_handlers();
  /* SIGTERM and SIGINT dump the boundary ring on the way out -- the same thing
     a fault prints, which is what says WHERE the run was spinning. */
  struct sigaction sa;
  memset(&sa, 0, sizeof sa);
  sa.sa_handler = interrupted;
  sigaction(SIGTERM, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
  return installed;
}

void trigger(X2FaultKind kind, bool genuine) {
  if (genuine && kind == X2_FAULT_ILL) {
#if defined(__aarch64__)
    __asm__ __volatile__(".inst 0");
#elif defined(__i386__) || defined(__x86_64__)
    __asm__ __volatile__("ud2");
#endif
  }
  raise(signal_of(kind));
}

bool run_child(int selftest_case, char *output, std::size_t capacity,
               ChildResult *result) {
  int fd[2];
  int status = 0;
  std::size_t got = 0;

  if (pipe(fd) != 0)
    return false;
  fflush(NULL);
  const pid_t pid = fork();
  if (pid < 0) {
    close(fd[0]);
    close(fd[1]);
    return false;
  }
  if (pid == 0) {
    close(fd[0]);
    dup2(fd[1], 2);
    close(fd[1]);
    _exit(selftest_child(selftest_case));
  }
  close(fd[1]);
  /* Drain to EOF even once the buffer is full: a child blocked writing into a
     pipe nobody reads would make waitpid() hang. */
  for (;;) {
    char sink[4096];
    ssize_t n;
    if (got < capacity - 1)
      n = read(fd[0], output + got, capacity - 1 - got);
    else
      n = read(fd[0], sink, sizeof sink);
    if (n <= 0)
      break;
    if (got < capacity - 1)
      got += (std::size_t)n;
  }
  output[got] = 0;
  close(fd[0]);
  waitpid(pid, &status, 0);
  result->bytes = got;
  result->exited = WIFEXITED(status);
  result->status = WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status);
  return true;
}

} // namespace x2::fault
