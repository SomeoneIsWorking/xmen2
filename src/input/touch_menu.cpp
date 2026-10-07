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

TouchMenuDelivery pad_edge(TouchMenuDelivery::Kind kind, TouchAction button) {
  TouchMenuDelivery delivery;
  delivery.kind = kind;
  delivery.button = button;
  return delivery;
}

TouchMenuDelivery click(presentation::ClientPoint at) {
  TouchMenuDelivery delivery;
  delivery.kind = TouchMenuDelivery::Kind::click;
  delivery.at = at;
  return delivery;
}

int row_of(const TouchMenuView &view, unsigned slot, int entry) {
  for (std::size_t i = 0; i < view.rows.size(); ++i) {
    if (view.rows[i].slot == slot && view.rows[i].entry == entry) {
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
  if (!same || view->detail != view_->detail) {
    detail_scroll_ = 0.0F;
  }
  std::vector<TouchMenuDelivery> owed;
  if (!view || view->address != held_address_) {
    release_held(&owed);
  }
  view_ = std::move(view);
  relayout();
  if (view_ && view_->focused_row >= 0 &&
      view_->focused_row != followed_focus_ && !finger_) {
    followed_focus_ = view_->focused_row;
    scroll_ = touch_menu_scroll_to(layout_, view_->focused_row);
    relayout();
  }
  for (TouchMenuDelivery &delivery : advance_walk(now_ms)) {
    owed.push_back(delivery);
  }
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
  layout_ = layout_touch_menu(*view_, viewport_, scroll_, detail_scroll_);
  scroll_ = layout_.scroll;
  detail_scroll_ = layout_.detail_scroll;
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
      const bool on_detail =
          layout_.detail_max_scroll > 0.0F && at.x >= layout_.detail.left &&
          at.x < layout_.detail.right && at.y >= layout_.detail.top &&
          at.y < layout_.detail.bottom;
      finger_ = Finger{id,
                       at,
                       on_detail ? detail_scroll_ : scroll_,
                       layout_.hit(at.x, at.y),
                       false,
                       on_detail};
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
  if (!finger.dragging && (on_list || finger.on_detail) &&
      std::fabs(at.y - finger.origin.y) > drag) {
    finger.dragging = true;
  }
  if (finger.dragging) {
    const float moved = finger.scroll_origin - (at.y - finger.origin.y);
    if (finger.on_detail) {
      detail_scroll_ = moved;
    } else {
      scroll_ = moved;
    }
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

void TouchMenu::release_held(std::vector<TouchMenuDelivery> *out) {
  if (held_) {
    out->push_back(pad_edge(TouchMenuDelivery::Kind::release, *held_));
    held_.reset();
  }
}

std::vector<TouchMenuDelivery>
TouchMenu::activate(const TouchMenuButton &button, std::uint64_t now_ms) {
  const auto index = static_cast<std::size_t>(button.index);
  std::vector<TouchMenuDelivery> out;
  if (button.part == TouchMenuPart::footer) {
    const TouchMenuFooter &footer = view_->footers[index];
    const bool letting_go = footer.held && held_ == footer.button;
    release_held(&out);
    if (letting_go) {
      return out;
    }
    if (footer.held && footer.button) {
      held_ = footer.button;
      held_address_ = view_->address;
      out.push_back(pad_edge(TouchMenuDelivery::Kind::press, *footer.button));
    } else if (footer.button) {
      out.push_back(pad(*footer.button));
    } else {
      out.push_back(click(footer.click));
    }
    return out;
  }
  release_held(&out);
  if (button.part == TouchMenuPart::tab) {
    walk_.reset();
    out.push_back(click(view_->tabs[index].click));
    return out;
  }
  const TouchMenuRow &row = view_->rows[index];
  if (button.part == TouchMenuPart::row && row.clicks) {
    walk_.reset();
    out.push_back(click(row.click));
    return out;
  }
  Walk walk;
  walk.address = view_->address;
  walk.slot = row.slot;
  walk.entry = row.entry;
  if (button.part == TouchMenuPart::step_left) {
    walk.final_button = TouchAction::MenuLeft;
  } else if (button.part == TouchMenuPart::step_right) {
    walk.final_button = TouchAction::MenuRight;
  } else if (row.press_on_arrival) {
    walk.final_button = TouchAction::MenuA;
  }
  walk.started_ms = now_ms;
  walk_ = walk;
  for (TouchMenuDelivery &delivery : advance_walk(now_ms)) {
    out.push_back(delivery);
  }
  return out;
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
  const int target_row = row_of(*view_, walk.slot, walk.entry);
  if (target_row < 0) {
    walk_.reset();
    return {};
  }
  const bool on_cycle = view_->cycle > 0;
  const int target =
      on_cycle ? view_->rows[static_cast<std::size_t>(target_row)].entry
               : target_row;
  const int focused = on_cycle ? view_->cycle_focus : view_->focused_row;
  if (focused == target) {
    const std::optional<TouchAction> final_button = walk.final_button;
    walk_.reset();
    if (!final_button) {
      return {};
    }
    return {pad(*final_button)};
  }
  if (walk.pressed && focused == walk.focus_at_press &&
      now_ms - walk.pressed_ms < kWalkStepMs) {
    return {};
  }
  bool down = focused < target;
  if (on_cycle || view_->focus_wraps) {
    const int count =
        on_cycle ? view_->cycle : static_cast<int>(view_->rows.size());
    const int down_steps = focused < 0 ? 0 : (target - focused + count) % count;
    const int up_steps =
        focused < 0 ? count : (focused - target + count) % count;
    down = down_steps <= up_steps;
  }
  walk.pressed = true;
  walk.pressed_ms = now_ms;
  walk.focus_at_press = focused;
  return {pad(down ? TouchAction::MenuDown : TouchAction::MenuUp)};
}

std::vector<TouchMenuDelivery> TouchMenu::cancel() {
  std::vector<TouchMenuDelivery> out;
  release_held(&out);
  finger_.reset();
  walk_.reset();
  publish();
  return out;
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
  for (std::size_t i = 0; held_ && i < state.view.footers.size(); ++i) {
    const TouchMenuFooter &footer = state.view.footers[i];
    if (footer.held && footer.button == held_) {
      state.held_footer = static_cast<int>(i);
    }
  }
  const std::lock_guard<std::mutex> lock(published_lock_);
  published_ = std::move(state);
}

TouchMenuState TouchMenu::state() const {
  const std::lock_guard<std::mutex> lock(published_lock_);
  return published_;
}

} // namespace x2::input
