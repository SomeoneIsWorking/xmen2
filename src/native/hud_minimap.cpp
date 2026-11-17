/* CHudAutoMap's round minimap, placed by the mobile HUD layout.
   external/xmen2-decomp/docs/notes/hud.md "Minimap" has the guest contract. */
#include "hud_minimap.hpp"

#include "guest_body.h"
#include "hud_draw_runtime.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "xmen2_symbols.hpp"

#include <lucent/log_c.h>

#include <mutex>

namespace x2::native {
namespace {

namespace exe = x2::symbols::xmen2;
inline constexpr float kCellWorldUnits = 240.0f; /* 0x0069ddf4 */

struct Frame {
  float x;
  float z;
  float scale;
  bool operator==(const Frame &) const = default;
};

Frame read_frame() {
  return {(float)RDF32(exe::g_AutoMapCentreX),
          (float)RDF32(exe::g_AutoMapCentreZ),
          (float)RDF32(exe::g_AutoMapScale)};
}

void write_frame(Frame frame) {
  WRF32(exe::g_AutoMapCentreX, frame.x);
  WRF32(exe::g_AutoMapCentreZ, frame.z);
  WRF32(exe::g_AutoMapScale, frame.scale);
}

/* Retail writes the centre and scale only when the mode is set (0x005a0150)
   and at init (0x005a0690). The move is applied before each update and
   undone when the mobile layout goes; a frame retail rewrote since is
   retail's new frame. */
class Minimap {
public:
  void update(CPU *cpu) {
    const Frame now = read_frame();
    if (placed_ && !(now == written_)) {
      placed_ = false;
    }
    HudDisc disc{};
    const bool mobile =
        RD32(exe::g_AutoMapMode) == 1u && hud_minimap_target(&disc);
    if (mobile) {
      if (!placed_) {
        retail_ = now;
      }
      written_ = moved(disc);
      write_frame(written_);
      if (!placed_) {
        lucent_log_info("hud",
                        "minimap moved to (%g,%g) radius %g, scale %g -> %g",
                        (double)disc.x, (double)disc.z, (double)disc.radius,
                        (double)retail_.scale, (double)written_.scale);
      }
      placed_ = true;
    } else if (placed_) {
      write_frame(retail_);
      placed_ = false;
    }
    publish(mobile ? &disc.box : nullptr);
    x86_guest_body(cpu, "XMen2.exe", exe::AutoMap_Update);
  }

  bool placed(x2::presentation::Rect *out) {
    const std::lock_guard<std::mutex> lock(lock_);
    if (shown_) {
      *out = box_;
    }
    return shown_;
  }

private:
  /* The map's world radius scales to the disc less half a marker, so a
     marker clamped to the rim stays inside it. */
  static Frame moved(const HudDisc &disc) {
    const float world =
        (float)((int)RD32(exe::g_AutoMapCells) - 1) * kCellWorldUnits;
    const float marker = (float)RDF32(exe::g_AutoMapMarkerWidth) * 0.5f;
    return {disc.x, disc.z, (disc.radius - marker) / world};
  }

  void publish(const x2::presentation::Rect *box) {
    const std::lock_guard<std::mutex> lock(lock_);
    shown_ = box != nullptr;
    if (box) {
      box_ = *box;
    }
  }

  bool placed_ = false;
  Frame retail_{};
  Frame written_{};
  std::mutex lock_;
  bool shown_ = false;
  x2::presentation::Rect box_{};
};

Minimap g_minimap;

void automap_update(CPU *cpu) { g_minimap.update(cpu); }

} // namespace

bool hud_minimap_placed(x2::presentation::Rect *out) {
  return g_minimap.placed(out);
}

} // namespace x2::native

__attribute__((constructor)) static void register_hud_minimap(void) {
  x86_register_override("XMen2.exe", x2::symbols::xmen2::AutoMap_Update,
                        x2::native::automap_update);
}
