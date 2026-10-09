#pragma once

/*
 * The off-screen target the pixel-reading self-tests share, and the one check
 * they all make: a named pixel holds an exact BGRA value.
 *
 * Shared rather than copied because the two owners -- the frame path in
 * gpu_selftest.cpp and the texture/combiner tests in gpu_texture_selftest.cpp
 * -- must read back at the SAME size. A test that quietly used a different
 * off-screen size would compare coordinates that mean different things.
 *
 * A failing check PRINTS what it got against what it wanted, so a failure
 * names the wrong colour instead of only saying that a test failed.
 */
#include <lucent/log_c.h>

#include <cstddef>
#include <cstdint>

namespace x2::gpu {

inline constexpr int OFF_W = 64;
inline constexpr int OFF_H = 64;

#ifdef X2_WITH_SDL
inline int px_is(const uint32_t *img, int x, int y, uint32_t bgra,
                 const char *what) {
  uint32_t got = img[(size_t)y * OFF_W + x];
  if (got == bgra)
    return 1;
  lucent_log_error(
      "x2",
      "gpu selftest: FAILED -- pixel (%d,%d) is 0x%08x, expected 0x%08x (%s)",
      x, y, got, bgra, what);
  return 0;
}
#endif

} // namespace x2::gpu
