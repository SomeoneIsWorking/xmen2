#pragma once

namespace x2::native {

/* The beat's Direct3D 8 pipeline lines: extra texture stages, combiner
   arguments, vertex-shader binding and constants, and dynamic buffer locks.
   Each line prints at zero too, and carries its delta since the last call. */
class HeartbeatRenderReport {
public:
  void report(unsigned long draws);

private:
  unsigned long multistage_ = 0;
  unsigned long unlocks_ = 0;
  unsigned long bytes_ = 0;
  unsigned long relocks_ = 0;
  unsigned long generations_ = 0;
};

} // namespace x2::native
