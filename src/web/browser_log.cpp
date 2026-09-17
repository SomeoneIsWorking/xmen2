#include "browser_log.hpp"

#include <SDL3/SDL_timer.h>
#include <emscripten/em_asm.h>
#include <lucent/log.h>

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

namespace x2::web {
namespace {

/* How much of the log to hold back before handing it to the page. */
constexpr std::size_t kLinesPerBlock = 64;
constexpr std::size_t kBytesPerBlock = 32u * 1024u;

/* Report the price of the proxying once in a while, so the browser run states
 * its own logging cost instead of leaving it as unaccounted wall time. */
constexpr std::size_t kLinesPerCostReport = 2000;

std::uint64_t monotonic_nanoseconds() {
  struct timespec now {};
  if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
    return 0;
  }
  return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL +
         static_cast<std::uint64_t>(now.tv_nsec);
}

/*
 * The page's console lives on the main browser thread; the game runs on a
 * pthread. Every line handed over is therefore a synchronous round trip
 * through Emscripten's proxying, and one per line made the browser build spend
 * most of its wall time waiting for the browser to come back -- the heartbeat
 * prints its ~65 lines in one burst, and the guest could not run again until
 * the last of them had been acknowledged.
 *
 * So batch. Each line keeps its own severity as a leading digit, which lets the
 * main thread split the block back into individual console messages: filters,
 * warnings and errors keep working, at one round trip per block instead of per
 * line. Errors flush immediately -- they are rare, and a run that stops has no
 * later opportunity to flush.
 */
class BatchConsoleSink {
public:
  void write(lucent::Level level, std::string_view line) {
    const bool error = level == lucent::Level::Error;
    std::lock_guard<std::mutex> guard(lock_);
    append_line(level, line);
    if (error || pending_lines_ >= kLinesPerBlock ||
        pending_.size() >= kBytesPerBlock) {
      flush_locked();
    }
  }

  /* Anything still batched when the page unloads would otherwise be lost. */
  void flush() {
    std::lock_guard<std::mutex> guard(lock_);
    flush_locked();
  }

private:
  /* Caller holds lock_. A non-recursive mutex is the only kind worth having
     here, and write() reaches this while holding it. */
  void flush_locked() {
    if (pending_.empty()) {
      return;
    }
    const std::uint64_t started = monotonic_nanoseconds();
    post_block(pending_);
    proxied_nanoseconds_ += monotonic_nanoseconds() - started;
    flushed_lines_ += pending_lines_;
    ++flushed_blocks_;
    pending_.clear();
    pending_lines_ = 0;
    if (flushed_lines_ >= next_cost_report_) {
      next_cost_report_ = flushed_lines_ + kLinesPerCostReport;
      char report[160];
      const unsigned long long millis = proxied_nanoseconds_ / 1000000ULL;
      std::snprintf(
          report, sizeof report,
          "web: log: %zu line(s) in %zu main-thread call(s), %llu ms spent "
          "waiting for the browser",
          flushed_lines_, flushed_blocks_, millis);
      append_line(lucent::Level::Info, report);
    }
  }

  void append_line(lucent::Level level, std::string_view line) {
    pending_.push_back(static_cast<char>('0' + static_cast<int>(level)));
    pending_.append(line);
    pending_.push_back('\n');
    ++pending_lines_;
  }

  static void post_block(const std::string &block) {
    MAIN_THREAD_EM_ASM(
        {
          for (const entry of UTF8ToString($0).split('\n')) {
            if (!entry)
              continue;
            const level = entry.charCodeAt(0) - 48;
            const text = entry.slice(1);
            if (level >= 3)
              console.error(text);
            else if (level == 2)
              console.warn(text);
            else
              console.log(text);
          }
        },
        block.c_str());
  }

  std::mutex lock_;
  std::string pending_;
  std::size_t pending_lines_ = 0;
  std::size_t flushed_lines_ = 0;
  std::size_t flushed_blocks_ = 0;
  std::uint64_t proxied_nanoseconds_ = 0;
  std::size_t next_cost_report_ = kLinesPerCostReport;
};

BatchConsoleSink &console_sink() {
  static BatchConsoleSink sink;
  return sink;
}

} // namespace

void install_browser_log_sink() {
  lucent::set_sink([](lucent::Level level, std::string_view line) {
    console_sink().write(level, line);
  });
  /* The batch otherwise flushes only at kLinesPerBlock/kBytesPerBlock, on an
     ERROR line, or when main() returns and calls flush_browser_log() once
     at the very end. A run that both logs fewer info lines than the batch
     threshold AND never returns from main() -- a hang, or one that outlives
     the page -- has its whole tail (a selftest's PASSED/FAILED result, or
     the reason a boot stalled) withheld from the console indefinitely,
     indistinguishable from silence. Diagnosing issue #152 spent real time
     on exactly that confusion. A bounded periodic flush, independent of
     main()'s own progress, caps how stale the console can ever be. */
  SDL_AddTimer(
      250,
      [](void *, SDL_TimerID, Uint32 interval) -> Uint32 {
        flush_browser_log();
        return interval;
      },
      nullptr);
}

void flush_browser_log() { console_sink().flush(); }

} // namespace x2::web
