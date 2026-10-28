#ifndef X2_TOUCH_MENU_SOURCE_HPP
#define X2_TOUCH_MENU_SOURCE_HPP

#include "../input/menu_touch_gaps.hpp"
#include "../presentation/retail_scene_plane.hpp"
#include "guest_memory_view.hpp"

#include <cstdint>
#include <optional>

namespace x2::native {

/* The retail scene plane as the running image presents it: the viewport
   singleton at 0x00a0a138 and the logical backbuffer at 0x00a09ffc/0x00a0a000
   (docs/RE/menus.md). */
std::optional<presentation::RetailScenePlane>
read_scene_plane(const GuestMemoryView &memory, std::uint32_t image_base);
/* The running image's scene plane. Guest thread only. */
std::optional<presentation::RetailScenePlane> live_scene_plane();

/*
 * While touch play is the input, reads the active retail menu at a bounded
 * cadence, tells the touch runtime whether a finger drives it directly, and
 * feeds the gap report. With touch play off it reads nothing. Guest thread
 * only.
 */
class TouchMenuSource {
public:
  void refresh(std::uint64_t now_ms);
  void report() const { gaps_.report(); }

private:
  input::MenuTouchGapReport gaps_;
  std::uint64_t last_ms_ = 0;
  bool offered_ = false;
};

/* The running game's source, polled by the Win32 event pump. */
void refresh_touch_menu();
/* Run end: what a finger could not do on the menus seen. */
void touch_menu_gap_report();

} // namespace x2::native

#endif
