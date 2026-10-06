#ifndef X2_DIAGNOSTICS_CRASH_REPORT_HPP
#define X2_DIAGNOSTICS_CRASH_REPORT_HPP

#include <cstdint>

namespace x2::diagnostics {

/* Async-signal-safe crash records written through RunLog's preopened
 * descriptors: no malloc, no stdio, no logger. */
class CrashReport {
public:
  /* Opens the record: a one-line header naming the signal, fault address and
   * program counter, then the host backtrace. Later logger lines are
   * mirrored into the crash file. */
  static void begin(const char *signal_name, int signal_number, int code,
                    std::uintptr_t address, std::uintptr_t pc);

  /* Takes SIGABRT: records it, then re-raises with the default action so the
   * exit status stays 134. A no-op where the platform has no sigaction. */
  static void install_abort_handler();

  /* Loads the unwinder now so the handler does not have to. */
  static void preload_backtrace();
};

} // namespace x2::diagnostics

#endif /* X2_DIAGNOSTICS_CRASH_REPORT_HPP */
