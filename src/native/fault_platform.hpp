/* The platform halves of the fault reporter: fault_signals_posix.cpp and
 * fault_signals_win32.cpp. fault_selftest.cpp drives the battery through
 * these; nothing else includes this. */
#ifndef X2_FAULT_PLATFORM_HPP
#define X2_FAULT_PLATFORM_HPP

#include "fault_report.h"

#include <cstddef>

namespace x2::fault {

/* Only the fatal-fault reporter, as a selftest child installs it. */
bool install_fatal_handlers();

/* Faults this process with `kind`, through a real faulting instruction when
 * `genuine` and the host's own raise otherwise. Returns only if nothing
 * fired. */
void trigger(X2FaultKind kind, bool genuine);

struct ChildResult {
  std::size_t bytes; /* stderr captured into the caller's buffer */
  bool exited;       /* left through exit rather than being killed */
  int status;        /* exit status, or the signal that killed it */
};

/* Runs selftest case `selftest_case` in a child process, capturing its stderr
 * into `output` (NUL-terminated). Returns false when no child could start. */
bool run_child(int selftest_case, char *output, std::size_t capacity,
               ChildResult *result);

} // namespace x2::fault

#endif /* X2_FAULT_PLATFORM_HPP */
