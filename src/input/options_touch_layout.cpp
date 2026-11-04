#include "options_touch_layout.hpp"

#include <algorithm>

namespace x2::input {
namespace {

/* A volume card's label line spans these shares of its height from the
   bottom, clear of the art's top frame; its bar sits about this far up. */
constexpr float kVolumeLabelBottom = 0.42F;
constexpr float kVolumeLabelTop = 0.9F;
constexpr float kVolumeBarMiddle = 0.27F;

} // namespace

CardStack layout_options_touch(const OptionsTouchInput &input) {
  const float half = 0.5F * kCardHeight * input.finger;
  const SceneBox column{input.left, input.accept_z + half, input.right,
                        input.title_z - half};
  return stack_cards(column, input.finger, input.rows, input.first, input.rows);
}

SceneBox volume_label(const SceneBox &card, float pad) {
  const SceneBox inner = card_text_box(card, pad);
  return {inner.left, card.bottom + kVolumeLabelBottom * card.height(),
          inner.right, card.bottom + kVolumeLabelTop * card.height()};
}

SceneBox volume_bar(const SceneBox &card, float pad, float height) {
  const float middle = card.bottom + kVolumeBarMiddle * card.height();
  const SceneBox inner = card_text_box(card, pad);
  return {inner.left, middle - 0.5F * height, inner.right,
          middle + 0.5F * height};
}

float bar_share(const SceneBox &bar, float x) {
  if (!(bar.width() > 0.0F)) {
    return 0.0F;
  }
  return std::clamp((x - bar.left) / bar.width(), 0.0F, 1.0F);
}

} // namespace x2::input
