/* The fault reporter: the report every fatal host fault prints, the platform
 * handlers that deliver faults to it (fault_signals_posix.cpp,
 * fault_signals_win32.cpp), and the --fault-selftest battery that proves they
 * fire (tests/fault_reporter in ctest).
 */
#pragma once

#include <cstdint>

namespace x2::fault {

/* The fatal fault classes, named as the POSIX signals that raise them. */
enum class FaultKind {
  Segv,
  Ill,
  Fpe,
  Bus,
  Trap,
  KindCount,
};

struct Fault {
  FaultKind kind;
  const char *meaning; /* the host's own account of this fault */
  int number;          /* host signal number or exception code */
  int code;            /* si_code, or the access kind of an access violation */
  std::uintptr_t address; /* memory address involved, or the instruction */
  std::uintptr_t pc;      /* host program counter; 0 when the host cannot say */
};

/* Prints the report for a fatal fault and leaves with exit status 3. */
[[noreturn]] void fault_report(const Fault *fault);
const char *fault_name(FaultKind kind);

/* Raises every fatal fault in a child and requires the report to name it;
 * a control child that faults nothing must stay silent. Returns 0 on pass. */
int fault_selftest();

/* Installs the reporter for every fatal fault, and the interrupt report for a
 * run that is stopped from outside. Returns false when the fault reporter
 * could not be installed. */
bool install_handlers();

/* The child half of the selftest where the host starts it as a new process
 * (--fault-selftest-child): `selftest_case` indexes the battery, and one past
 * its end is the control that faults nothing. */
int selftest_child(int selftest_case);

} // namespace x2::fault
