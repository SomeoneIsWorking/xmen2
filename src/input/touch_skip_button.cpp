#include "touch_skip_button.h"

#include "cutscene_skip.h"

namespace x2::input {
namespace {

/* The button's height as a fraction of the viewport's, and its width as a
   multiple of that height: a pill wide enough for its word and an arrow. */
constexpr float kHeightFraction = 0.11F;
constexpr float kWidthPerHeight = 2.4F;
/* Clear of the corner by a fraction of the height, as the other controls sit
   clear of the edge. */
constexpr float kMarginFraction = 0.035F;

bool inside(X2Rect rect, float x, float y) {
  return x >= rect.left && x < rect.right && y >= rect.top && y < rect.bottom;
}

bool is_release(lucent::touch::Phase phase) {
  return phase == lucent::touch::Phase::ended ||
         phase == lucent::touch::Phase::canceled;
}

} // namespace

X2Rect SkipButton::place(X2LayoutViewport viewport) {
  const float height = viewport.height * kHeightFraction;
  const float width = height * kWidthPerHeight;
  const float margin = viewport.height * kMarginFraction;
  const float right = viewport.width - viewport.safe_right - margin;
  const float top = viewport.safe_top + margin;
  return X2Rect{right - width, top, right, top + height};
}

bool SkipButton::offered() { return x2_cutscene_skip_available() != 0; }

bool SkipButton::press(std::int64_t contact_id, float x, float y,
                       lucent::touch::Phase phase, X2LayoutViewport viewport) {
  if (contact_) {
    if (*contact_ != contact_id) {
      return false;
    }
    if (is_release(phase)) {
      contact_.reset();
    }
    return true;
  }
  if (phase != lucent::touch::Phase::began || !offered() ||
      !inside(place(viewport), x, y)) {
    return false;
  }
  contact_ = contact_id;
  x2_cutscene_skip_request();
  return true;
}

} // namespace x2::input
