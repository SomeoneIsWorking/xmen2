#include "popup_touch_layout.hpp"

#include <algorithm>
#include <cmath>

namespace x2::input {
namespace {

/* The share of the screen the grown frame may take. */
constexpr float kFrameFillWidth = 0.94F;
constexpr float kFrameFillHeight = 0.90F;
/* Card and button heights, and the gap between them, in fingers. */
constexpr float kCardHeight = 1.0F;
constexpr float kCardGap = 0.15F;
constexpr float kButtonHeight = 1.0F;
constexpr float kButtonGap = 0.3F;
/* Card text height in fingers; the arrow column's width. */
constexpr float kCardText = 0.42F;
constexpr float kArrowColumn = 0.7F;
/* The title against retail's, grown with the frame. */
constexpr float kTitleScale = 1.4F;
/* Room left over so the wanted cards still fit after rounding. */
constexpr float kFitSlack = 0.05F;
/* The shortest frame against its width scale, so the art keeps its shape. */
constexpr float kMinHeight = 0.35F;

} // namespace

PopupTouchLayout layout_popup_touch(const PopupTouchInput &input) {
  PopupTouchLayout out;
  const presentation::RetailScenePlane &plane = input.plane;
  const SceneBox &native = input.frame;
  if (!(native.width() > 0.0F) || !(native.height() > 0.0F) ||
      !(input.line > 0.0F) || !(input.finger > 0.0F)) {
    return out;
  }
  const float finger = input.finger;
  /* Retail's own insets inside the art: the left margin, also kept at the
     right and bottom, and the header down to the title's middle. */
  const float margin = input.text_left - native.left;
  const float header = native.top - input.text_top;
  /* The frame spans the screen's width; it is as tall as what it holds. */
  const float scale_x =
      std::min(kFrameFillWidth * plane.width() / native.width(),
               kFrameFillHeight * plane.height() / native.height());
  const float max_z = kFrameFillHeight * plane.height() / native.height();
  out.title_scale = kTitleScale * scale_x;
  out.text_scale = kCardText * finger / input.line;
  const float card = kCardHeight * finger;
  const float gap = kCardGap * finger;
  out.pitch = card + gap;
  const int wanted = std::clamp(input.entries, 0, std::max(1, input.max_rows));
  const float content =
      0.5F * input.line * out.title_scale +
      static_cast<float>(wanted) * out.pitch +
      (input.footers > 0 ? kButtonHeight * finger + gap : 0.0F) +
      kFitSlack * finger;
  const float art = native.height() - header - margin;
  const float scale_z =
      std::clamp(content / std::max(art, 1.0F), kMinHeight * scale_x, max_z);
  out.frame_scale = {scale_x, scale_z};

  const float centre_x = plane.left() + 0.5F * plane.width();
  const float centre_z = 0.5F * plane.height();
  out.frame_offset = {centre_x - scale_x * 0.5F * (native.left + native.right),
                      centre_z - scale_z * 0.5F * (native.bottom + native.top)};
  out.frame = {out.frame_offset.x + scale_x * native.left,
               out.frame_offset.z + scale_z * native.bottom,
               out.frame_offset.x + scale_x * native.right,
               out.frame_offset.z + scale_z * native.top};
  const float inset = scale_x * margin;
  out.title = {out.frame.left + inset, out.frame.top - scale_z * header};

  const float body_left = out.frame.left + inset;
  const float body_right = out.frame.right - inset;
  const float body_bottom = out.frame.bottom + scale_z * margin;
  const float body_top =
      out.title.z - 0.5F * input.line * out.title_scale - kCardGap * finger;

  const float button_top =
      input.footers > 0 ? body_bottom + kButtonHeight * finger : body_bottom;
  if (input.footers > 0) {
    const float apart = kButtonGap * finger;
    const float width = (body_right - body_left -
                         apart * static_cast<float>(input.footers - 1)) /
                        static_cast<float>(input.footers);
    for (int i = 0; i < input.footers; ++i) {
      const float left = body_left + static_cast<float>(i) * (width + apart);
      out.buttons.push_back({left, body_bottom, left + width, button_top});
    }
  }

  const float list_bottom = input.footers > 0 ? button_top + gap : body_bottom;
  out.rows = std::clamp(
      static_cast<int>(std::floor((body_top - list_bottom + gap) / out.pitch)),
      1, std::max(1, input.max_rows));
  const int last_first = std::max(0, input.entries - out.rows);
  out.first_shown = std::clamp(input.first_shown, 0, last_first);
  const float card_right = input.entries > out.rows
                               ? body_right - kArrowColumn * finger
                               : body_right;
  const int shown = std::min(out.rows, input.entries - out.first_shown);
  for (int i = 0; i < shown; ++i) {
    const float top = body_top - static_cast<float>(i) * out.pitch;
    out.cards.push_back({body_left, top - card, card_right, top});
  }
  out.more_above = out.first_shown > 0;
  out.more_below = out.first_shown + shown < input.entries;
  const float arrow_x = 0.5F * (card_right + body_right);
  out.arrow_up = {arrow_x, body_top - 0.5F * card};
  out.arrow_down = {arrow_x,
                    body_top -
                        static_cast<float>(std::max(shown, 1) - 1) * out.pitch -
                        0.5F * card};
  return out;
}

int popup_touch_scrolled(const PopupTouchLayout &layout, int entries,
                         int start_first, float dz) {
  if (!(layout.pitch > 0.0F)) {
    return start_first;
  }
  /* Content follows the finger: dragging up shows later entries. */
  const auto rows = static_cast<int>(std::lround(dz / layout.pitch));
  return std::clamp(start_first + rows, 0, std::max(0, entries - layout.rows));
}

int popup_touch_hit(const std::vector<SceneBox> &boxes,
                    presentation::ScenePoint point) {
  for (std::size_t i = 0; i < boxes.size(); ++i) {
    if (boxes[i].contains(point)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

} // namespace x2::input
