#include "host_sampler.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>

#if defined(__linux__) && (defined(__aarch64__) || defined(__x86_64__))
#define X2_HOST_SAMPLER 1
#include <csignal>
#include <ctime>
#include <link.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>
#endif

namespace x2::native {

namespace {

/* What the SIGPROF handler writes into; a handler can reach nothing else. */
std::atomic<HostSampler::Sample *> g_buffer{nullptr};
std::atomic<std::size_t> g_next{0};
std::size_t g_capacity = 0;
/* The sampled thread's stack: a frame pointer outside it is not followed, so
   a register JIT code uses for something else is never dereferenced. */
std::uintptr_t g_stack_low = 0;
std::uintptr_t g_stack_high = 0;

HostSampler g_sampler;

#ifdef X2_HOST_SAMPLER
void on_sample(int, siginfo_t *, void *context) {
  HostSampler::Sample *buffer = g_buffer.load(std::memory_order_acquire);
  if (!buffer) {
    return;
  }
  const std::size_t at = g_next.fetch_add(1, std::memory_order_relaxed);
  if (at >= g_capacity) {
    return;
  }
  const auto *uc = static_cast<const ucontext_t *>(context);
  HostSampler::Sample &sample = buffer[at];
#if defined(__aarch64__)
  sample.pc = uc->uc_mcontext.pc;
  sample.lr = uc->uc_mcontext.regs[30];
  const std::uintptr_t sp = uc->uc_mcontext.sp;
  std::uintptr_t fp = uc->uc_mcontext.regs[29];
#else
  const auto sp = static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RSP]);
  sample.pc = static_cast<std::uint64_t>(uc->uc_mcontext.gregs[REG_RIP]);
  sample.lr = sp >= g_stack_low && sp + 8 <= g_stack_high
                  ? *reinterpret_cast<const std::uint64_t *>(sp)
                  : 0;
  std::uintptr_t fp =
      static_cast<std::uintptr_t>(uc->uc_mcontext.gregs[REG_RBP]);
#endif
  /* Only a chain that starts on the sampled stack, at or above the
     interrupted SP, is walked; each link must climb. */
  const bool on_stack =
      g_stack_low != 0 && sp >= g_stack_low && sp < g_stack_high;
  for (std::size_t i = 0; i < HostSampler::kFrames; i++) {
    if (!on_stack || fp < sp || fp + 16 > g_stack_high || (fp & 7u) != 0) {
      sample.frames[i] = 0;
      continue;
    }
    const auto *frame = reinterpret_cast<const std::uintptr_t *>(fp);
    sample.frames[i] = frame[1];
    fp = frame[0] > fp ? frame[0] : 0;
  }
}

int add_module(dl_phdr_info *info, size_t, void *out) {
  std::uint64_t low = UINT64_MAX;
  std::uint64_t high = 0;
  for (int i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr) &ph = info->dlpi_phdr[i];
    if (ph.p_type != PT_LOAD) {
      continue;
    }
    low = std::min<std::uint64_t>(low, info->dlpi_addr + ph.p_vaddr);
    high = std::max<std::uint64_t>(high,
                                   info->dlpi_addr + ph.p_vaddr + ph.p_memsz);
  }
  if (low < high) {
    char line[600];
    char exe[512];
    const char *name = info->dlpi_name;
    if (!name || !*name) {
      /* The executable itself comes with an empty name. */
      const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
      name = n > 0 ? exe : "[main]";
      exe[n > 0 ? n : 0] = '\0';
    }
    std::snprintf(line, sizeof line, "module %llx %llx %llx %s\n",
                  static_cast<unsigned long long>(info->dlpi_addr),
                  static_cast<unsigned long long>(low),
                  static_cast<unsigned long long>(high), name);
    static_cast<std::string *>(out)->append(line);
  }
  return 0;
}
#endif

} // namespace

HostSampler &host_sampler() { return g_sampler; }

HostSampler::~HostSampler() { stop(); }

std::size_t HostSampler::taken() const {
  return std::min(g_next.load(std::memory_order_relaxed), samples_.size());
}

#ifdef X2_HOST_SAMPLER
bool HostSampler::start(unsigned hz, std::size_t capacity, std::string &why) {
  if (hz == 0 || hz > 10000 || capacity == 0) {
    why = "hz must be 1..10000 and the sample count positive";
    return false;
  }
  stop();
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) != 0) {
    why = "pthread_getattr_np failed; the frame chain has no bounds";
    return false;
  }
  void *stack = nullptr;
  std::size_t stack_size = 0;
  pthread_attr_getstack(&attr, &stack, &stack_size);
  pthread_attr_destroy(&attr);
  g_stack_low = reinterpret_cast<std::uintptr_t>(stack);
  g_stack_high = g_stack_low + stack_size;
  samples_.assign(capacity, Sample{});
  g_buffer.store(nullptr, std::memory_order_release);
  g_capacity = capacity;
  g_next.store(0, std::memory_order_relaxed);
  g_buffer.store(samples_.data(), std::memory_order_release);

  struct sigaction action = {};
  action.sa_sigaction = on_sample;
  action.sa_flags = SA_SIGINFO | SA_RESTART;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGPROF, &action, nullptr) != 0) {
    why = "sigaction(SIGPROF) failed";
    return false;
  }
  sigevent event = {};
  event.sigev_notify = SIGEV_THREAD_ID;
  event.sigev_signo = SIGPROF;
  const auto tid = static_cast<pid_t>(syscall(SYS_gettid));
#if defined(__GLIBC__)
  event._sigev_un._tid =
      tid; /* glibc gives SIGEV_THREAD_ID's field no public name */
#else
  event.sigev_notify_thread_id = tid;
#endif
  timer_t timer = nullptr;
  if (timer_create(CLOCK_THREAD_CPUTIME_ID, &event, &timer) != 0) {
    why = "timer_create(CLOCK_THREAD_CPUTIME_ID) failed";
    return false;
  }
  const long interval_ns = 1000000000L / static_cast<long>(hz);
  itimerspec spec = {};
  spec.it_interval.tv_nsec = interval_ns;
  spec.it_value.tv_nsec = interval_ns;
  if (timer_settime(timer, 0, &spec, nullptr) != 0) {
    timer_delete(timer);
    why = "timer_settime failed";
    return false;
  }
  timer_ = timer;
  hz_ = hz;
  timer_armed_ = true;
  return true;
}

void HostSampler::stop() {
  if (!timer_armed_) {
    return;
  }
  timer_delete(static_cast<timer_t>(timer_));
  timer_ = nullptr;
  timer_armed_ = false;
}

std::string HostSampler::report() const {
  std::string out;
  char bounds[96];
  std::snprintf(bounds, sizeof bounds, "stack %llx %llx\n",
                static_cast<unsigned long long>(g_stack_low),
                static_cast<unsigned long long>(g_stack_high));
  out.append(bounds);
  dl_iterate_phdr(add_module, &out);
  const std::size_t n = taken();
  char word[24];
  for (std::size_t i = 0; i < n; i++) {
    const Sample &sample = samples_[i];
    std::snprintf(word, sizeof word, "sample %llx",
                  static_cast<unsigned long long>(sample.pc));
    out.append(word);
    std::snprintf(word, sizeof word, " %llx",
                  static_cast<unsigned long long>(sample.lr));
    out.append(word);
    for (std::uint64_t ret : sample.frames) {
      if (!ret) {
        break;
      }
      std::snprintf(word, sizeof word, " %llx",
                    static_cast<unsigned long long>(ret));
      out.append(word);
    }
    out.push_back('\n');
  }
  return out;
}
#else
bool HostSampler::start(unsigned, std::size_t, std::string &why) {
  why = "the in-process sampler needs Linux or Android on arm64 or x86_64";
  return false;
}

void HostSampler::stop() {}

std::string HostSampler::report() const { return {}; }
#endif

} // namespace x2::native
