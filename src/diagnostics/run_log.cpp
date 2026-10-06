#include "run_log.hpp"

#include <lucent/log.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string_view>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <sys/stat.h>
#if defined(_WIN32)
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace x2::diagnostics {

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kLogPrefix = "x2native-";
constexpr std::string_view kLogSuffix = ".log";
constexpr std::string_view kCrashPrefix = "crash-";
constexpr std::string_view kCrashSuffix = ".txt";

std::atomic<int> g_log_fd{-1};
std::atomic<int> g_crash_fd{-1};
std::atomic<bool> g_fatal{false};
std::string g_log_path;
std::string g_crash_path;
/* The crash path again, in storage a signal handler may read. */
std::array<char, 4096> g_crash_path_signal{};
bool g_exit_hook = false;

int current_pid() {
#if defined(_WIN32)
  return _getpid();
#else
  return static_cast<int>(getpid());
#endif
}

std::string utc_stamp(std::chrono::system_clock::time_point now) {
  const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  char text[32];
  std::strftime(text, sizeof text, "%Y%m%dT%H%M%SZ", &utc);
  return text;
}

bool has_affixes(const std::string &name, std::string_view prefix,
                 std::string_view suffix) {
  return name.size() > prefix.size() + suffix.size() &&
         name.compare(0, prefix.size(), prefix) == 0 &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string stem_of_log(const std::string &name) {
  return name.substr(kLogPrefix.size(),
                     name.size() - kLogPrefix.size() - kLogSuffix.size());
}

std::string crash_name(const std::string &stem) {
  return std::string(kCrashPrefix) + stem + std::string(kCrashSuffix);
}

/* Keeps the newest keep-1 runs so this run makes keep; a pruned log takes its
 * crash file with it. Stamps sort lexicographically, so name order is age. */
void prune(const fs::path &directory, std::size_t keep) {
  std::vector<std::string> logs;
  std::error_code ec;
  for (const fs::directory_entry &entry :
       fs::directory_iterator(directory, ec)) {
    const std::string name = entry.path().filename().string();
    if (has_affixes(name, kLogPrefix, kLogSuffix)) {
      logs.push_back(name);
    }
  }
  std::sort(logs.begin(), logs.end());
  const std::size_t retained = keep > 0 ? keep - 1 : 0;
  if (logs.size() <= retained) {
    return;
  }
  for (std::size_t index = 0; index < logs.size() - retained; ++index) {
    fs::remove(directory / logs[index], ec);
    fs::remove(directory / crash_name(stem_of_log(logs[index])), ec);
  }
}

void write_all(int fd, const char *data, std::size_t length) {
  while (length > 0) {
    const auto written = ::write(fd, data, static_cast<unsigned>(length));
    if (written <= 0) {
      return;
    }
    data += written;
    length -= static_cast<std::size_t>(written);
  }
}

int open_append(const std::string &path) {
#if defined(_WIN32)
  return _open(path.c_str(), _O_WRONLY | _O_CREAT | _O_APPEND | _O_BINARY,
               _S_IREAD | _S_IWRITE);
#else
  return ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
#endif
}

void tee_line(lucent::Level, std::string_view line) {
  std::string text(line);
  text.push_back('\n');
  write_all(2, text.data(), text.size());
  const int log_fd = g_log_fd.load(std::memory_order_relaxed);
  if (log_fd >= 0) {
    write_all(log_fd, text.data(), text.size());
  }
  if (g_fatal.load(std::memory_order_relaxed)) {
    const int crash_fd = g_crash_fd.load(std::memory_order_relaxed);
    if (crash_fd >= 0) {
      write_all(crash_fd, text.data(), text.size());
    }
  }
}

} // namespace

bool RunLog::start(const Options &options, std::string &error) {
  stop();
  const fs::path &directory = options.directory;
  std::error_code ec;
  fs::create_directories(directory, ec);
  if (ec) {
    error = "cannot create " + directory.string() + ": " + ec.message();
    return false;
  }
  prune(directory, options.keep);

  const int pid = options.pid != 0 ? options.pid : current_pid();
  const std::string stem = utc_stamp(options.now) + "-" + std::to_string(pid);
  const std::string log_path =
      (directory / (std::string(kLogPrefix) + stem + std::string(kLogSuffix)))
          .string();
  const std::string crash_path = (directory / crash_name(stem)).string();
  const int log_fd = open_append(log_path);
  if (log_fd < 0) {
    error = "cannot open " + log_path;
    return false;
  }
  if (crash_path.size() >= g_crash_path_signal.size()) {
    error = "crash path too long: " + crash_path;
    ::close(log_fd);
    return false;
  }
  g_log_path = log_path;
  g_crash_path = crash_path;
  crash_path.copy(g_crash_path_signal.data(), crash_path.size());
  g_crash_path_signal[crash_path.size()] = '\0';
  g_fatal.store(false);
  g_log_fd.store(log_fd);
  lucent::set_sink(tee_line);
  if (!g_exit_hook) {
    g_exit_hook = true;
    std::atexit(RunLog::stop);
  }
  return true;
}

void RunLog::stop() {
  const int log_fd = g_log_fd.exchange(-1);
  const int crash_fd = g_crash_fd.exchange(-1);
  if (log_fd < 0) {
    return;
  }
  lucent::set_sink(nullptr);
  ::close(log_fd);
  if (crash_fd >= 0) {
    ::close(crash_fd);
  }
  g_crash_path_signal[0] = '\0';
}

const std::string &RunLog::log_path() { return g_log_path; }

const std::string &RunLog::crash_path() { return g_crash_path; }

int RunLog::log_descriptor() {
  return g_log_fd.load(std::memory_order_relaxed);
}

int RunLog::crash_descriptor() {
  return g_crash_fd.load(std::memory_order_relaxed);
}

void RunLog::write_fatal(const char *data, std::size_t length) {
  write_all(2, data, length);
  const int log_fd = g_log_fd.load(std::memory_order_relaxed);
  if (log_fd >= 0) {
    write_all(log_fd, data, length);
  }
  const int crash_fd = g_crash_fd.load(std::memory_order_relaxed);
  if (crash_fd >= 0) {
    write_all(crash_fd, data, length);
  }
}

void RunLog::mark_fatal() {
  /* open(2) is async-signal-safe, so the crash file exists only for a crash. */
  if (g_crash_fd.load() < 0 && g_crash_path_signal[0] != '\0') {
    const int crash_fd = open_append(g_crash_path_signal.data());
    int none = -1;
    if (crash_fd >= 0 && !g_crash_fd.compare_exchange_strong(none, crash_fd)) {
      ::close(crash_fd);
    }
  }
  g_fatal.store(true, std::memory_order_relaxed);
}

} // namespace x2::diagnostics
