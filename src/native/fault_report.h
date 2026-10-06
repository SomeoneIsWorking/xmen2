/* The fault reporter: the report every fatal host fault prints, the platform
 * handlers that deliver faults to it (fault_signals_posix.cpp,
 * fault_signals_win32.cpp), and the --fault-selftest battery that proves they
 * fire (tests/fault_reporter in ctest).
 */
#ifndef X2_FAULT_REPORT_H
#define X2_FAULT_REPORT_H

#include <stdint.h>

/* The fatal fault classes, named as the POSIX signals that raise them. */
typedef enum X2FaultKind {
  X2_FAULT_SEGV,
  X2_FAULT_ILL,
  X2_FAULT_FPE,
  X2_FAULT_BUS,
  X2_FAULT_TRAP,
  X2_FAULT_KIND_COUNT
} X2FaultKind;

typedef struct X2Fault {
  X2FaultKind kind;
  const char *meaning; /* the host's own account of this fault */
  int number;          /* host signal number or exception code */
  int code;            /* si_code, or the access kind of an access violation */
  uintptr_t address;   /* memory address involved, or the instruction */
  uintptr_t pc;        /* host program counter; 0 when the host cannot say */
} X2Fault;

/* Prints the report for a fatal fault and leaves with exit status 3. */
[[noreturn]] void fault_report(const X2Fault *fault);
const char *fault_name(X2FaultKind kind);

/* Raises every fatal fault in a child and requires the report to name it;
 * a control child that faults nothing must stay silent. Returns 0 on pass. */
int x2_fault_selftest(void);

namespace x2::fault {

/* Installs the reporter for every fatal fault, and the interrupt report for a
 * run that is stopped from outside. Returns false when the fault reporter
 * could not be installed. */
bool install_handlers();

/* The child half of the selftest where the host starts it as a new process
 * (--fault-selftest-child): `selftest_case` indexes the battery, and one past
 * its end is the control that faults nothing. */
int selftest_child(int selftest_case);

} // namespace x2::fault

#endif /* X2_FAULT_REPORT_H */
