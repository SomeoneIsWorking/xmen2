#ifndef X2_TOUCH_MENU_SOURCE_HPP
#define X2_TOUCH_MENU_SOURCE_HPP

#include "../input/touch_menu_view.hpp"
#include "guest_memory_view.hpp"

#include <cstdint>
#include <optional>

namespace x2::native {

/* The retail scene plane as the running image presents it: the viewport
   singleton at 0x00a0a138 and the logical backbuffer at 0x00a09ffc/0x00a0a000
   (docs/RE/menus.md). */
std::optional<presentation::RetailScenePlane>
read_scene_plane(const GuestMemoryView &memory, std::uint32_t image_base);

/*
 * Feeds the touch menu: while touch play is the input, reads the active retail
 * menu and the scene plane at a bounded cadence and hands the touch runtime
 * the view of a menu it replaces. With touch play off it reads nothing.
 * Guest thread only.
 */
class TouchMenuSource {
public:
  void refresh(std::uint64_t now_ms);

private:
  std::uint64_t last_ms_ = 0;
  bool offered_ = false;
};

/* The running game's source, polled by the Win32 event pump. */
void refresh_touch_menu();

} // namespace x2::native

#endif
