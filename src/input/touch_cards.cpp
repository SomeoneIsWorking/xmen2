#include "touch_cards.hpp"

#include <algorithm>
#include <cmath>

namespace x2::input {

int card_rows(float height, float finger, int max_rows) {
  const float gap = kCardGap * finger;
  const float pitch = kCardHeight * finger + gap;
  return std::clamp(static_cast<int>(std::floor((height + gap) / pitch)), 1,
                    std::max(1, max_rows));
}

CardStack stack_cards(const SceneBox &area, float finger, int entries,
                      int first, int max_rows) {
  CardStack out;
  if (!(finger > 0.0F)) {
    return out;
  }
  const float card = kCardHeight * finger;
  out.pitch = card + kCardGap * finger;
  out.rows = card_rows(area.height(), finger, max_rows);
  out.entries = std::max(0, entries);
  out.first = std::clamp(first, 0, std::max(0, out.entries - out.rows));
  const int shown = std::min(out.rows, out.entries - out.first);
  for (int i = 0; i < shown; ++i) {
    const float top = area.top - static_cast<float>(i) * out.pitch;
    out.cards.push_back({area.left, top - card, area.right, top});
  }
  out.more_above = out.first > 0;
  out.more_below = out.first + shown < out.entries;
  return out;
}

int cards_scrolled(const CardStack &stack, int start_first, float dz) {
  if (!(stack.pitch > 0.0F)) {
    return start_first;
  }
  /* Content follows the finger: dragging up shows later entries. */
  const auto rows = static_cast<int>(std::lround(dz / stack.pitch));
  return std::clamp(start_first + rows, 0,
                    std::max(0, stack.entries - stack.rows));
}

int box_hit(const std::vector<SceneBox> &boxes,
            presentation::ScenePoint point) {
  for (std::size_t i = 0; i < boxes.size(); ++i) {
    if (boxes[i].contains(point)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

} // namespace x2::input
