#include "crash_report.hpp"
#include "run_log.hpp"
#include "x2_log.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "ucrt_abort_status.hpp"
#include <process.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

namespace fs = std::filesystem;
using x2::diagnostics::CrashReport;
using x2::diagnostics::RunLog;

int g_failures = 0;

void expect(bool condition, const std::string &what) {
  if (!condition) {
    std::fprintf(stderr, "test_run_log: FAILED: %s\n", what.c_str());
    ++g_failures;
  }
}

std::string read_file(const fs::path &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream text;
  text << in.rdbuf();
  return text.str();
}

std::vector<std::string> names_with_suffix(const fs::path &directory,
                                           const std::string &suffix) {
  std::vector<std::string> names;
  for (const auto &entry : fs::directory_iterator(directory)) {
    const std::string name = entry.path().filename().string();
    if (name.size() > suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
      names.push_back(name);
    }
  }
  return names;
}

RunLog::Options options_for(const fs::path &root, int run) {
  RunLog::Options options;
  options.directory = root / "logs";
  options.keep = 10;
  options.now = std::chrono::system_clock::time_point(
      std::chrono::seconds(1'700'000'000 + run * 60));
  options.pid = 4000 + run;
  return options;
}

void test_retention_and_flush(const fs::path &root) {
  std::string error;
  for (int run = 0; run < 13; ++run) {
    expect(RunLog::start(options_for(root, run), error), "start: " + error);
    x2_log_info("run %d", run);
  }
  const fs::path logs = root / "logs";
  std::vector<std::string> names = names_with_suffix(logs, ".log");
  expect(names.size() == 10,
         "retention keeps 10 logs, found " + std::to_string(names.size()));
  expect(fs::exists(RunLog::log_path()), "newest log exists");
  const std::string &newest = RunLog::log_path();
  expect(newest.find("-4012.log") != std::string::npos,
         "newest log carries its pid: " + newest);
  expect(names_with_suffix(logs, "-4000.log").empty() &&
             names_with_suffix(logs, "-4002.log").empty(),
         "the three oldest logs were pruned");
  expect(names_with_suffix(logs, "-4003.log").size() == 1,
         "the fourth-oldest log was kept");

  /* Read while the run is live: the line must already be on disk. */
  x2_log_info("flushed %s", "marker");
  const std::string content = read_file(newest);
  expect(content.find("[x2] run 12") != std::string::npos &&
             content.find("[x2] flushed marker") != std::string::npos,
         "log lines are on disk without stop(): " + content);
  expect(content.find("T") != std::string::npos &&
             content.find("Z] [x2]") != std::string::npos,
         "lines keep the logger's UTC prefix");
  expect(names_with_suffix(logs, ".txt").empty(),
         "a live run has no crash file until it crashes");
  RunLog::stop();
  expect(names_with_suffix(logs, ".txt").empty(),
         "a clean run leaves no empty crash file behind");
}

enum class Crash { Abort, Segv };

constexpr char kAbortChild[] = "--abort-child";

/* The crashing process: a run log, the abort handler, one line, the crash. */
[[noreturn]] void crash_child(const fs::path &dir, Crash kind);

void expect_crash_record(const fs::path &dir, Crash kind) {
  const fs::path logs = dir / "logs";
  const std::vector<std::string> crashes = names_with_suffix(logs, ".txt");
  const std::vector<std::string> run_logs = names_with_suffix(logs, ".log");
  expect(crashes.size() == 1 && run_logs.size() == 1,
         "one crash file and one log file written");
  if (crashes.size() != 1 || run_logs.size() != 1) {
    return;
  }
  const std::string crash = read_file(logs / crashes[0]);
  const std::string log = read_file(logs / run_logs[0]);
  const std::string name = kind == Crash::Abort ? "SIGABRT" : "SIGSEGV";
  expect(crash.find("*** CRASH " + name) == 0, "crash file opens with " + name);
  expect(crash.find("pc=0x") != std::string::npos, "crash file has the pc");
#if !defined(__ANDROID__) && !defined(_WIN32)
  expect(crash.find("[HOST STACK]") != std::string::npos,
         "crash file has the host backtrace");
#endif
  expect(log.find("before the crash") != std::string::npos &&
             log.find("*** CRASH " + name) != std::string::npos,
         "run log holds the lines before the crash and the record");
}

#if defined(_WIN32)
void crash_child(const fs::path &dir, Crash) {
  std::string error;
  RunLog::Options options;
  options.directory = dir / "logs";
  if (!RunLog::start(options, error)) {
    _exit(10);
  }
  CrashReport::install_abort_handler();
  x2_log_info("before the crash");
  abort();
}

void test_crash(const char *self, const fs::path &root) {
  const fs::path dir = root / "abort";
  const std::string quoted = "\"" + dir.string() + "\"";
  const intptr_t status =
      _spawnl(_P_WAIT, self, self, kAbortChild, quoted.c_str(), nullptr);
  expect(x2::test::is_ucrt_abort_status(status),
         "abort keeps the UCRT's abort status after the report");
  expect_crash_record(dir, Crash::Abort);
}
#else

void segv_handler(int sig, siginfo_t *info, void *) {
  CrashReport::begin("SIGSEGV", sig, info->si_code,
                     reinterpret_cast<std::uintptr_t>(info->si_addr), 0);
  _exit(3);
}

void crash_child(const fs::path &dir, Crash kind) {
  std::string error;
  RunLog::Options options;
  options.directory = dir / "logs";
  if (!RunLog::start(options, error)) {
    _exit(10);
  }
  CrashReport::preload_backtrace();
  CrashReport::install_abort_handler();
  x2_log_info("before the crash");
  if (kind == Crash::Abort) {
    abort();
  }
  struct sigaction action{};
  action.sa_sigaction = segv_handler;
  action.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &action, nullptr);
  raise(SIGSEGV);
  _exit(11);
}

void test_crash(const fs::path &root, Crash kind) {
  const fs::path dir = root / (kind == Crash::Abort ? "abort" : "segv");
  const pid_t child = fork();
  if (child == 0) {
    crash_child(dir, kind);
  }
  int status = 0;
  waitpid(child, &status, 0);
  if (kind == Crash::Abort) {
    expect(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
           "abort keeps its SIGABRT death after the report");
  } else {
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 3,
           "segv handler ran and exited 3");
  }
  expect_crash_record(dir, kind);
}
#endif

} // namespace

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == kAbortChild) {
    crash_child(argv[2], Crash::Abort);
  }
  if (argc != 2) {
    std::fprintf(stderr, "usage: test_run_log <scratch directory>\n");
    return 2;
  }
  const fs::path root = argv[1];
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root);
  test_retention_and_flush(root / "retention");
#if defined(_WIN32)
  test_crash(argv[0], root);
#else
  test_crash(root, Crash::Abort);
  test_crash(root, Crash::Segv);
#endif
  return g_failures == 0 ? 0 : 1;
}
