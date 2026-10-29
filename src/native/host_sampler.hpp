#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace x2::native {

/*
 * A CPU-time sampling profile of one host thread, taken inside the process.
 *
 * Some phones refuse perf_event_open to both shell and the app, so simpleperf
 * cannot profile there. This arms a timer on the calling thread's CPU clock;
 * each expiry's SIGPROF records the interrupted PC, link register and the
 * frame-pointer chain's return addresses. The report lists every loaded
 * module beside the samples, so a reader resolves native code against the
 * unstripped library and JIT code against jit.map
 * (`tools/jit_map_profile.py --samples`).
 */
class HostSampler {
public:
  /* Return addresses kept from the frame-pointer chain above the PC. */
  static constexpr std::size_t kFrames = 12;
  /* One expiry's interrupted host PC, link register and frame-chain returns
     (zero past the chain's end). */
  struct Sample {
    std::uint64_t pc;
    std::uint64_t lr;
    std::uint64_t frames[kFrames];
  };

  HostSampler() = default;
  HostSampler(const HostSampler &) = delete;
  HostSampler &operator=(const HostSampler &) = delete;
  ~HostSampler();

  /* Sample the calling thread `hz` times per CPU second until `capacity`
     samples are held. False with `why` when the host cannot. */
  bool start(unsigned hz, std::size_t capacity, std::string &why);
  /* Stop the timer; the samples stay readable. */
  void stop();
  bool armed() const { return timer_armed_; }
  std::size_t taken() const;
  std::size_t capacity() const { return samples_.size(); }
  unsigned hz() const { return hz_; }
  /* "module BIAS LOW HIGH PATH" lines, then "sample PC LR RET..." lines
     (hex). */
  std::string report() const;

private:
  std::vector<Sample> samples_;
  unsigned hz_ = 0;
  bool timer_armed_ = false;
  void *timer_ = nullptr;
};

/* The run's one sampler, driven by /performance/sample. */
HostSampler &host_sampler();

} // namespace x2::native
