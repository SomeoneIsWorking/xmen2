#include "diagnostics_startup.hpp"

#include "crash_report.hpp"
#include "environment.h"
#include "run_log.hpp"
#include "x2_log.h"

#include <filesystem>
#include <string>

namespace x2::diagnostics {

void Startup::begin(const char *user_data) {
  const char *override_directory =
      config_override_get(x2::config::ConfigOverride::LogDir);
  RunLog::Options options;
  if (override_directory != nullptr && override_directory[0] != '\0') {
    options.directory = override_directory;
  } else if (user_data != nullptr && user_data[0] != '\0') {
    options.directory = std::filesystem::path(user_data) / "logs";
  } else {
    x2_log_error("run log: no user-data directory; logging to stderr only");
    return;
  }
  options.keep = kKeepRuns;
  std::string error;
  if (!RunLog::start(options, error)) {
    x2_log_error("run log: %s; logging to stderr only", error.c_str());
    return;
  }
  CrashReport::preload_backtrace();
  CrashReport::install_abort_handler();
  x2_log_info("run log: %s (crash reports: %s)", RunLog::log_path().c_str(),
              RunLog::crash_path().c_str());
}

} // namespace x2::diagnostics
