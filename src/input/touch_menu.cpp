#include "touch_menu.hpp"

#include <cmath>

namespace x2::input {
namespace {

/* Travel, in design units, that turns a press on the list into a scroll. */
constexpr float kDragUnits = 14.0F;
/* How long one Up/Down may take to move the game's focus before it is sent
   again. */
constexpr std::uint64_t kWalkStepMs = 600u;
/* A walk that has not reached its row by then is abandoned. */
constexpr std::uint64_t kWalkLimitMs = 5000u;

TouchMenuDelivery pad(TouchAction button) {
  TouchMenuDelivery delivery;
  delivery.kind = TouchMenuDelivery::Kind::pad;
  delivery.button = button;
  return delivery;
}

TouchMenuDelivery click(presentation::ClientPoint at) {
  TouchMenuDelivery delivery;
  delivery.kind = TouchMenuDelivery::Kind::click;
  delivery.at = at;
  return delivery;
}

int row_of_slot(const TouchMenuView &view, unsigned slot) {
  for (std::size_t i = 0; i < view.rows.size(); ++i) {
    if (view.rows[i].slot == slot) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

} // namespace

std::vector<TouchMenuDelivery>
TouchMenu::set_view(std::optional<TouchMenuView> view, std::uint64_t now_ms) {
  const bool same =
      view.has_value() && view_.has_value() && view->same_screen(*view_);
  if (!same) {
    scroll_ = 0.0F;
    followed_focus_ = -1;
    finger_.reset();
  }
  view_ = std::move(view);
  relayout();
  if (view_ && view_->focused_row >= 0 &&
      view_->focused_row != followed_focus_ && !finger_) {
    followed_focus_ = view_->focused_row;
    scroll_ = touch_menu_scroll_to(layout_, view_->focused_row);
    relayout();
  }
  std::vector<TouchMenuDelivery> owed = advance_walk(now_ms);
  publish();
  return owed;
}

void TouchMenu::set_viewport(const X2LayoutViewport &viewport) {
  viewport_ = viewport;
  has_viewport_ = viewport.width > 0.0F && viewport.height > 0.0F;
  finger_.reset();
  relayout();
  publish();
}

void TouchMenu::relayout() {
  if (!view_ || !has_viewport_) {
    layout_ = TouchMenuLayout{};
    return;
  }
  layout_ = layout_touch_menu(*view_, viewport_, scroll_);
  scroll_ = layout_.scroll;
}

std::vector<TouchMenuDelivery> TouchMenu::contact(std::int64_t id,
                                                  lucent::touch::Point at,
                                                  lucent::touch::Phase phase,
                                                  std::uint64_t now_ms) {
  std::vector<TouchMenuDelivery> out;
  if (!shown()) {
    finger_.reset();
    return out;
  }
  if (phase == lucent::touch::Phase::began) {
    if (!finger_) {
      finger_ = Finger{id, at, scroll_, layout_.hit(at.x, at.y), false};
      publish();
    }
    return out;
  }
  if (!finger_ || finger_->id != id) {
    return out;
  }
  Finger &finger = *finger_;
  const float drag = kDragUnits * layout_.unit;
  const bool on_list = finger.origin.x >= layout_.list.left &&
                       finger.origin.x < layout_.list.right &&
                       finger.origin.y >= layout_.list.top &&
                       finger.origin.y < layout_.list.bottom;
  if (!finger.dragging && on_list && std::fabs(at.y - finger.origin.y) > drag) {
    finger.dragging = true;
  }
  if (finger.dragging) {
    scroll_ = finger.scroll_origin - (at.y - finger.origin.y);
    relayout();
  }
  if (phase == lucent::touch::Phase::moved) {
    publish();
    return out;
  }
  const bool tapped = phase == lucent::touch::Phase::ended &&
                      !finger.dragging && finger.button.has_value() &&
                      layout_.hit(at.x, at.y) == finger.button;
  if (tapped) {
    out = activate(layout_.buttons[*finger.button], now_ms);
  }
  finger_.reset();
  publish();
  return out;
}

std::vector<TouchMenuDelivery>
TouchMenu::activate(const TouchMenuButton &button, std::uint64_t now_ms) {
  const auto index = static_cast<std::size_t>(button.index);
  if (button.part == TouchMenuPart::footer) {
    return {click(view_->footers[index].click)};
  }
  const TouchMenuRow &row = view_->rows[index];
  if (button.part == TouchMenuPart::row && row.clicks) {
    walk_.reset();
    return {click(row.click)};
  }
  Walk walk;
  walk.address = view_->address;
  walk.slot = row.slot;
  walk.final_button =
      button.part == TouchMenuPart::step_left    ? TouchAction::MenuLeft
      : button.part == TouchMenuPart::step_right ? TouchAction::MenuRight
                                                 : TouchAction::MenuA;
  walk.started_ms = now_ms;
  walk_ = walk;
  return advance_walk(now_ms);
}

std::vector<TouchMenuDelivery> TouchMenu::advance_walk(std::uint64_t now_ms) {
  if (!walk_) {
    return {};
  }
  Walk &walk = *walk_;
  if (!view_ || view_->address != walk.address ||
      now_ms - walk.started_ms > kWalkLimitMs) {
    walk_.reset();
    return {};
  }
  const int target = row_of_slot(*view_, walk.slot);
  if (target < 0) {
    walk_.reset();
    return {};
  }
  const int focused = view_->focused_row;
  if (focused == target) {
    const TouchAction final_button = walk.final_button;
    walk_.reset();
    return {pad(final_button)};
  }
  if (walk.pressed && focused == walk.focus_at_press &&
      now_ms - walk.pressed_ms < kWalkStepMs) {
    return {};
  }
  const int count = static_cast<int>(view_->rows.size());
  const int down = focused < 0 ? 0 : (target - focused + count) % count;
  const int up = focused < 0 ? count : (focused - target + count) % count;
  walk.pressed = true;
  walk.pressed_ms = now_ms;
  walk.focus_at_press = focused;
  return {pad(down <= up ? TouchAction::MenuDown : TouchAction::MenuUp)};
}

void TouchMenu::cancel() {
  finger_.reset();
  walk_.reset();
  publish();
}

void TouchMenu::publish() {
  TouchMenuState state;
  state.shown = shown();
  state.viewport = viewport_;
  if (view_) {
    state.view = *view_;
  }
  state.layout = layout_;
  state.pressed = finger_ && finger_->button && !finger_->dragging
                      ? static_cast<int>(*finger_->button)
                      : -1;
  const std::lock_guard<std::mutex> lock(published_lock_);
  published_ = std::move(state);
}

TouchMenuState TouchMenu::state() const {
  const std::lock_guard<std::mutex> lock(published_lock_);
  return published_;
}

} // namespace x2::input
