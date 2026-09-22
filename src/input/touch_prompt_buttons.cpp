#include "touch_prompt_buttons.h"

#include "touch_census.h"
#include "touch_runtime.h"

extern "C" {
#include "../native/guest_clock.h"
}

#include <algorithm>
#include <cmath>

namespace {
X2TouchKeyPress g_press;
} // namespace

void x2_touch_prompt_key_press(X2TouchKeyPress press) { g_press = press; }

namespace x2::input {
namespace {

bool live_at(const PromptButton &button, double now) {
  return button.dik != 0 && now - button.at < X2_TOUCH_PROMPT_LIFETIME &&
         now >= button.at;
}

bool contains(const X2Rect &rect, float x, float y) {
  return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

} // namespace

void PromptButtons::publish(X2LayoutViewport viewport, X2Rect drawn,
                            unsigned dik, double now) {
  if (dik == 0 || !std::isfinite(drawn.left) || !std::isfinite(drawn.top) ||
      !std::isfinite(drawn.right) || !std::isfinite(drawn.bottom) ||
      drawn.right <= drawn.left || drawn.bottom <= drawn.top) {
    return;
  }
  const X2Rect target = x2_layout_touch_target(viewport, drawn);
  /* The same key twice is the same prompt drawn again, not a second control:
     a screen redraws its footer every frame, and keeping both would leave a
     stale rectangle pressable wherever the text had moved to. */
  auto slot = std::find_if(
      buttons_.begin(), buttons_.end(),
      [dik](const PromptButton &button) { return button.dik == dik; });
  if (slot == buttons_.end()) {
    slot = std::find_if(
        buttons_.begin(), buttons_.end(),
        [now](const PromptButton &button) { return !live_at(button, now); });
  }
  if (slot == buttons_.end()) {
    /* Every slot holds a live prompt of its own. Refusing keeps the ones
       already on screen answering, which is the safer of the two losses. */
    return;
  }
  *slot = {target, dik, now};
}

unsigned PromptButtons::key_at(float x, float y, double now) const {
  for (const auto &button : buttons_) {
    if (live_at(button, now) && contains(button.target, x, y)) {
      return button.dik;
    }
  }
  return 0;
}

bool PromptButtons::press(std::int64_t contact_id, float x, float y,
                          lucent::touch::Phase phase, double now) {
  const bool release = phase == lucent::touch::Phase::ended ||
                       phase == lucent::touch::Phase::canceled;
  if (release) {
    return holding_.erase(contact_id) != 0;
  }
  if (holding_.contains(contact_id)) {
    return true;
  }
  if (phase != lucent::touch::Phase::began) {
    return false;
  }
  const unsigned dik = key_at(x, y, now);
  if (!dik) {
    return false;
  }
  holding_.insert(contact_id);
  X2TouchCensus &census = *x2_touch_census();
  if (!g_press || !g_press(dik, now)) {
    census.prompt_refused++;
    return true;
  }
  census.prompt_presses++;
  return true;
}

void PromptButtons::release() { holding_.clear(); }

std::size_t PromptButtons::live(PromptButton *out, std::size_t capacity,
                                double now) const {
  std::size_t count = 0;
  for (const auto &button : buttons_) {
    if (!live_at(button, now)) {
      continue;
    }
    if (out && count < capacity) {
      out[count] = button;
    }
    ++count;
  }
  return count;
}

void PromptButtons::clear() { buttons_ = {}; }

PromptButtons &prompt_buttons() {
  static PromptButtons buttons;
  return buttons;
}

} // namespace x2::input

void x2_touch_prompt_publish(X2Rect drawn, unsigned dik, double now) {
  X2LayoutViewport viewport;
  if (x2_touch_runtime_viewport(&viewport)) {
    x2::input::prompt_buttons().publish(viewport, drawn, dik, now);
  }
}

size_t x2_touch_prompts_live(X2TouchPromptInfo *out, size_t capacity) {
  std::array<x2::input::PromptButton, X2_TOUCH_PROMPTS_MAX> live{};
  const std::size_t count = x2::input::prompt_buttons().live(
      live.data(), live.size(), guest_clock_now_s());
  for (std::size_t i = 0; i < count && i < capacity && i < live.size(); ++i) {
    out[i] = {live[i].target, live[i].dik};
  }
  return count;
}
