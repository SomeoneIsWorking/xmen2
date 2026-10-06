/* Internal to the fault reporter: the handler x2native installs for every
 * fatal signal, the name table it reports from, and the --fault-selftest
 * battery that proves both fire (tests/fault_reporter in ctest).
 */
#ifndef X2_FAULT_REPORT_H
#define X2_FAULT_REPORT_H

#include <signal.h>

void fault_report(int sig, siginfo_t *si, void *uc);
const char *fault_name(int sig);

/* Raises every fatal signal in a child and requires the report to name it;
 * a control child that faults nothing must stay silent. Returns 0 on pass. */
int x2_fault_selftest(void);

#endif /* X2_FAULT_REPORT_H */
