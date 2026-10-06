#ifndef X2_DIAGNOSTICS_STARTUP_HPP
#define X2_DIAGNOSTICS_STARTUP_HPP

namespace x2::diagnostics {

/* Composes the run log and the crash reporter for the process entry point. */
class Startup {
public:
  /* Run logs kept per user-data directory, this run included. */
  static constexpr unsigned kKeepRuns = 10;

  /* Starts file logging in `<user_data>/logs` (X2_LOG_DIR overrides), arms
   * the abort recorder, then logs where the run's files are. Logging stays on
   * stderr alone, with one error line, when the directory is missing or
   * unwritable. */
  static void begin(const char *user_data);
};

} // namespace x2::diagnostics

#endif /* X2_DIAGNOSTICS_STARTUP_HPP */
