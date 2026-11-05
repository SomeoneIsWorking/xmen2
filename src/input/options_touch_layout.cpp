#include "options_touch_layout.hpp"

#include <algorithm>

namespace x2::input {
namespace {

/* A volume card's label line and its bar span these shares of its height
   from the bottom, both inside the panel's ring (0.22 to 0.78); the bar's
   ends sit this much further in than the text. */
constexpr float kVolumeLabelBottom = 0.47F;
constexpr float kVolumeLabelTop = 0.75F;
constexpr float kVolumeBarBottom = 0.29F;
constexpr float kVolumeBarTop = 0.42F;
constexpr float kVolumeBarInset = 0.5F;

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

SceneBox volume_bar(const SceneBox &card, float pad) {
  const SceneBox inner = card_text_box(card, pad * (1.0F + kVolumeBarInset));
  return {inner.left, card.bottom + kVolumeBarBottom * card.height(),
          inner.right, card.bottom + kVolumeBarTop * card.height()};
}

float bar_share(const SceneBox &bar, float x) {
  if (!(bar.width() > 0.0F)) {
    return 0.0F;
  }
  return std::clamp((x - bar.left) / bar.width(), 0.0F, 1.0F);
}

} // namespace x2::input
