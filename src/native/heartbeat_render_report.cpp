#include "heartbeat_render_report.h"

#include "d3d8_device.h"
#include "d3d8_drawcall.h"
#include "d3d8_resource.h"
#include "d3d8_vertex_shader.h"
#include "x2_log.h"

#include <stdint.h>

namespace x2::native {

void HeartbeatRenderReport::report(unsigned long draws) {
  unsigned long ms = 0;
  int most = 0;
  d3d8_drawcall_multistage(&ms, &most);
  x2_log_error("[HB]           %lu of %lu draw(s) (+%lu) wanted a texture "
               "stage beyond 0 (up to %d extra); stage 1 is implemented and "
               "stage 2+ is refused\n",
               ms, draws, ms - multistage_, most);
  multistage_ = ms;

  unsigned long dflt = 0;
  unsigned long other = 0;
  uint32_t f[4] = {};
  d3d8_drawcall_combiner_args(&dflt, &other, f);
  /* The shader assumes the default arguments; zero others licenses that. */
  x2_log_error("[HB]           combiner args: %lu default, %lu other%s\n", dflt,
               other, other ? "" : " -- the shader's assumption holds");
  if (other) {
    x2_log_error("[HB]             first non-default: COLORARG1 %u COLORARG2 "
                 "%u ALPHAARG1 %u ALPHAARG2 %u\n",
                 f[0], f[1], f[2], f[3]);
  }

  char vsl[256];
  d3d8_vertex_shader_binding_line(vsl, sizeof vsl);
  x2_log_error("[HB]           %s\n", vsl);
  char pl[192];
  d3d8_vsconst_caller_line(pl, sizeof pl);
  x2_log_error("[HB]           %s\n", pl);

  unsigned long lk = 0;
  unsigned long dis = 0;
  unsigned long noov = 0;
  unsigned long unl = 0;
  unsigned long byt = 0;
  unsigned long rel = 0;
  unsigned long gen = 0;
  d3d8_buffer_lock_counts(&lk, &dis, &noov, &unl, &byt, &rel, &gen);
  x2_log_error("[HB]           buffer locks %lu (%lu DISCARD, %lu "
               "NOOVERWRITE); %lu unlock(s) (+%lu) moved %lu MB (+%lu MB); "
               "%lu (+%lu) relocked in one frame\n",
               lk, dis, noov, unl, unl - unlocks_, byt >> 20,
               (byt - bytes_) >> 20, rel, rel - relocks_);
  /* A write after a draw needs SDL_GPU to cycle to a new backing buffer. */
  x2_log_error("[HB]           of those unlocks, %lu (+%lu) needed a NEW "
               "BUFFER GENERATION after an earlier draw -- SDL_GPU cycling "
               "preserves both\n",
               gen, gen - generations_);
  unlocks_ = unl;
  bytes_ = byt;
  relocks_ = rel;
  generations_ = gen;
}

} // namespace x2::native
