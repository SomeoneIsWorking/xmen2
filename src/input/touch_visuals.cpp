#include "touch_visuals.h"

namespace x2::input {
namespace {

/* Prompt identifiers live above every layout zone's, so one set of overlay
   elements can carry both without a collision. RmlUi keeps an element per id,
   and the key a prompt names is what makes that id stable while the control
   is on screen. */
constexpr std::uint32_t kPromptVisualId = 1000;

} // namespace

std::size_t overlay_visuals(std::span<const TouchControls::ZoneVisual> zones,
                            const std::set<std::uint32_t> &active,
                            ThumbStick::Deflection stick, X2Rect stick_ring,
                            std::span<const PromptButton> prompts,
                            X2TouchVisual *out, std::size_t capacity) {
  std::size_t count = 0;
  const auto emit = [&](const X2TouchVisual &visual) {
    if (out && count < capacity) {
      out[count] = visual;
    }
    ++count;
  };
  for (const auto &zone : zones) {
    if (!zone.visible) {
      continue;
    }
    /* The stick's zone is where a thumb may land; the ring is drawn apart. */
    const X2Rect drawn = zone.stick ? stick_ring
                                    : X2Rect{zone.zone.left, zone.zone.top,
                                             zone.zone.right, zone.zone.bottom};
    emit({zone.zone.id, drawn.left, drawn.top, drawn.right, drawn.bottom,
          static_cast<int>(zone.action), active.contains(zone.zone.id) ? 1 : 0,
          zone.stick ? X2_TOUCH_VISUAL_STICK : X2_TOUCH_VISUAL_BUTTON,
          zone.stick ? stick.x : 0.0F, zone.stick ? stick.y : 0.0F,
          zone.power_icon});
  }
  for (const auto &prompt : prompts) {
    emit({kPromptVisualId + prompt.dik, prompt.target.left, prompt.target.top,
          prompt.target.right, prompt.target.bottom, 0, 0,
          X2_TOUCH_VISUAL_PROMPT, 0.0F, 0.0F, -1});
  }
  return count;
}

} // namespace x2::input
