#include "crash_report.hpp"

#include "run_log.hpp"

#include <cstddef>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define X2_CRASH_SIGNALS 1
#include <signal.h>
#include <unistd.h>
#if !defined(__ANDROID__)
#include <execinfo.h>
#define X2_CRASH_BACKTRACE 1
#endif
#endif

namespace x2::diagnostics {

namespace {

/* A fixed buffer formatted without libc, so it is safe in a handler. */
class SignalText {
public:
  void text(const char *value) {
    while (*value != '\0' && length_ < sizeof buffer_) {
      buffer_[length_++] = *value++;
    }
  }

  void decimal(long value) {
    if (value < 0) {
      text("-");
      value = -value;
    }
    number(static_cast<unsigned long long>(value), 10);
  }

  void hex(std::uintptr_t value) {
    text("0x");
    number(value, 16);
  }

  void flush() const { RunLog::write_fatal(buffer_, length_); }

private:
  void number(unsigned long long value, unsigned base) {
    char digits[24];
    std::size_t count = 0;
    do {
      digits[count++] = "0123456789abcdef"[value % base];
      value /= base;
    } while (value != 0 && count < sizeof digits);
    while (count > 0 && length_ < sizeof buffer_) {
      buffer_[length_++] = digits[--count];
    }
  }

  char buffer_[256];
  std::size_t length_ = 0;
};

#if defined(X2_CRASH_BACKTRACE)
void write_backtrace() {
  void *frames[32];
  const int count = backtrace(frames, 32);
  SignalText header;
  header.text("[HOST STACK] ");
  header.decimal(count);
  header.text(" frame(s):\n");
  header.flush();
  /* backtrace_symbols_fd takes one descriptor; the crash file gets the same
   * frames the log and stderr do by way of three calls. */
  backtrace_symbols_fd(frames, count, 2);
  const int log_fd = RunLog::log_descriptor();
  if (log_fd >= 0) {
    backtrace_symbols_fd(frames, count, log_fd);
  }
  const int crash_fd = RunLog::crash_descriptor();
  if (crash_fd >= 0) {
    backtrace_symbols_fd(frames, count, crash_fd);
  }
}
#endif

#if defined(X2_CRASH_SIGNALS)
void on_abort(int, siginfo_t *, void *) {
  CrashReport::begin("SIGABRT", SIGABRT, 0, 0, 0);
  signal(SIGABRT, SIG_DFL);
  raise(SIGABRT);
}
#endif

} // namespace

void CrashReport::begin(const char *signal_name, int signal_number, int code,
                        std::uintptr_t address, std::uintptr_t pc) {
  RunLog::mark_fatal();
  SignalText header;
  header.text("*** CRASH ");
  header.text(signal_name);
  header.text(" (");
  header.decimal(signal_number);
  header.text(") code=");
  header.decimal(code);
  header.text(" addr=");
  header.hex(address);
  header.text(" pc=");
  header.hex(pc);
  header.text("\n");
  header.flush();
#if defined(X2_CRASH_BACKTRACE)
  write_backtrace();
#endif
}

void CrashReport::install_abort_handler() {
#if defined(X2_CRASH_SIGNALS)
  struct sigaction action{};
  action.sa_sigaction = on_abort;
  action.sa_flags = SA_SIGINFO | SA_NODEFER;
  sigaction(SIGABRT, &action, nullptr);
#endif
}

void CrashReport::preload_backtrace() {
#if defined(X2_CRASH_BACKTRACE)
  void *frame = nullptr;
  backtrace(&frame, 1);
#endif
}

} // namespace x2::diagnostics
