#include "touch_menu_source.hpp"

#include "../input/touch_runtime.h"
#include "../input/touch_runtime_menu.hpp"
#include "x86rt_native.h"

#include <SDL3/SDL.h>

#include <cstring>

namespace x2::native {
namespace {

inline constexpr std::uint32_t kViewportRva = 0x0060a138u;
inline constexpr std::uint32_t kViewportAspect = 0x10u;
inline constexpr std::uint32_t kViewportScaleX = 0x48u;
inline constexpr std::uint32_t kViewportScaleZ = 0x4cu;
inline constexpr std::uint32_t kClientWidthRva = 0x00609ffcu;
inline constexpr std::uint32_t kClientHeightRva = 0x0060a000u;
/* The menu model is a few hundred guest reads; a 30 Hz read keeps the touch
   menu and a focus walk current. */
inline constexpr std::uint64_t kRefreshMs = 33u;

bool read_f32(const GuestMemoryView &memory, std::uint32_t address,
              float *out) {
  std::uint32_t bits = 0u;
  if (!memory.read_u32(address, &bits)) {
    return false;
  }
  std::memcpy(out, &bits, sizeof bits);
  return true;
}

TouchMenuSource source;

} // namespace

std::optional<presentation::RetailScenePlane>
read_scene_plane(const GuestMemoryView &memory, std::uint32_t image_base) {
  float aspect = 0.0F;
  float scale_x = 0.0F;
  float scale_z = 0.0F;
  std::uint32_t client_w = 0u;
  std::uint32_t client_h = 0u;
  const std::uint32_t viewport = image_base + kViewportRva;
  if (!read_f32(memory, viewport + kViewportAspect, &aspect) ||
      !read_f32(memory, viewport + kViewportScaleX, &scale_x) ||
      !read_f32(memory, viewport + kViewportScaleZ, &scale_z) ||
      !memory.read_u32(image_base + kClientWidthRva, &client_w) ||
      !memory.read_u32(image_base + kClientHeightRva, &client_h)) {
    return std::nullopt;
  }
  return presentation::RetailScenePlane::from_viewport(aspect, scale_x, scale_z,
                                                       client_w, client_h);
}

void TouchMenuSource::refresh(std::uint64_t now_ms) {
  if (!x2::input::touch_runtime_active()) {
    if (offered_) {
      offered_ = false;
      input::touch_runtime_set_menu(std::nullopt);
    }
    return;
  }
  if (offered_ && now_ms - last_ms_ < kRefreshMs) {
    return;
  }
  last_ms_ = now_ms;
  offered_ = true;
  std::optional<input::TouchMenuView> view;
  const std::uint32_t image = x86_module_base("XMen2.exe");
  menu::MenuSnapshot snapshot;
  std::uint32_t failed = 0u;
  if (image != 0u &&
      menu::read_live_menu(&snapshot, &failed) == menu::ReadStatus::ok) {
    const LiveGuestMemory memory;
    if (const auto plane = read_scene_plane(memory, image)) {
      view = input::build_touch_menu_view(snapshot, *plane);
    }
  }
  input::touch_runtime_set_menu(std::move(view));
}

void refresh_touch_menu() { source.refresh(SDL_GetTicks()); }

} // namespace x2::native
