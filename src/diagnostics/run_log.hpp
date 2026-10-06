#ifndef X2_DIAGNOSTICS_RUN_LOG_HPP
#define X2_DIAGNOSTICS_RUN_LOG_HPP

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>

namespace x2::diagnostics {

/* Tees everything the project logger emits into
 * <directory>/x2native-<UTC timestamp>-<pid>.log and owns the preopened
 * descriptors the crash reporter writes through. The write(2) entry points
 * are async-signal-safe; start/stop are not. */
class RunLog {
public:
  struct Options {
    std::filesystem::path directory; /* holds the run logs */
    std::size_t keep = 10;
    std::chrono::system_clock::time_point now =
        std::chrono::system_clock::now();
    int pid = 0; /* 0: this process */
  };

  /* Prunes old runs, opens this run's log and crash files and installs the
   * logger sink. Returns false with the reason in `error`. */
  static bool start(const Options &options, std::string &error);

  /* Restores the default logger sink, closes both files and removes this
   * run's crash file if no crash was recorded. Registered with atexit. */
  static void stop();

  static const std::string &log_path();
  static const std::string &crash_path();

  /* Preopened descriptors, -1 when no run log is active. */
  static int log_descriptor();
  static int crash_descriptor();

  /* Async-signal-safe: stderr, the log file and the crash file. */
  static void write_fatal(const char *data, std::size_t length);

  /* Async-signal-safe: later logger lines are mirrored into the crash file. */
  static void mark_fatal();
};

} // namespace x2::diagnostics

#endif /* X2_DIAGNOSTICS_RUN_LOG_HPP */
