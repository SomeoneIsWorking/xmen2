/*
 * The --fault-selftest battery: it proves the fault reporter fires, which is a
 * different concern from installing it. Each case runs in a child process the
 * platform half starts (fault_platform.hpp); the judgement is shared.
 */
#include "fault_platform.hpp"
#include "fault_report.h"
#include "x2_log.h"

#include "platform_posix.h"
#include <stdio.h>
#include <string.h>

namespace {

struct SelftestCase {
  X2FaultKind kind;
  bool genuine;
  const char *what;
};

constexpr SelftestCase kCases[] = {
    {X2_FAULT_ILL, true, "a real illegal opcode instruction"},
    {X2_FAULT_FPE, false, "a raised arithmetic fault"},
    {X2_FAULT_BUS, false, "a raised bus/alignment fault"},
    {X2_FAULT_TRAP, false, "a raised trap"},
    {X2_FAULT_SEGV, false, "a raised access fault"},
};
constexpr int kCaseCount = static_cast<int>(sizeof kCases / sizeof kCases[0]);

} // namespace

namespace x2::fault {

int selftest_child(int selftest_case) {
  if (selftest_case < 0 || selftest_case > kCaseCount) {
    return 2;
  }
  install_fatal_handlers();
  if (selftest_case < kCaseCount) {
    trigger(kCases[selftest_case].kind, kCases[selftest_case].genuine);
  }
  fflush(NULL);
  _exit(0); /* the handler leaves with 3; reaching here for a fault fails */
}

} // namespace x2::fault

int x2_fault_selftest(void) {
  int fails = 0;

  for (int i = 0; i <= kCaseCount; i++) {
    const bool control = i == kCaseCount;
    char output[8192];
    x2::fault::ChildResult result{};
    if (!x2::fault::run_child(i, output, sizeof output, &result)) {
      x2_log_info("x2native --fault-selftest: no child process could be "
                  "started; NOTHING was checked.\n");
      return 1;
    }
    if (control) {
      const bool quiet = strstr(output, "***") == NULL;
      const bool clean = result.exited && result.status == 0;
      x2_log_info("  control: handlers installed, no fault  -- %s "
                  "(%zu byte(s) on stderr, exit %d)\n",
                  quiet && clean ? "silent, as it must be"
                                 : "FAILED: it reported a fault that did not "
                                   "happen",
                  result.bytes, result.exited ? result.status : -1);
      if (!quiet || !clean) {
        fails++;
      }
      continue;
    }
    const char *want = fault_name(kCases[i].kind);
    const bool named = strstr(output, want) != NULL;
    const bool host_pc = strstr(output, "[HOST PC]") != NULL;
    const bool reported = result.exited && result.status == 3;
    x2_log_info("  %-8s via %-32s -- %s (exit %d, %zu byte(s) reported)\n",
                want, kCases[i].what,
                named && host_pc && reported
                    ? "reported by name with host PC"
                    : "FAILED: no report reached stderr",
                result.exited ? result.status : -result.status, result.bytes);
    if (!named || !host_pc || !reported) {
      fails++;
    }
  }
  x2_log_info(
      "x2native --fault-selftest: %s -- %d failure(s). Before this, only "
      "SIGSEGV was handled and every other fatal signal killed the run "
      "with nothing printed.\n",
      fails ? "FAILED" : "PASSED", fails);
  return fails ? 1 : 0;
}
