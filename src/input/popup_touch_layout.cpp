#include "popup_touch_layout.hpp"

#include <algorithm>

namespace x2::input {
namespace {

/* The share of the screen the grown frame may take. */
constexpr float kFrameFillWidth = 0.94F;
constexpr float kFrameFillHeight = 0.90F;
/* Button height and the gap between buttons, in fingers; the arrow
   column's width. */
constexpr float kButtonHeight = 1.0F;
constexpr float kButtonGap = 0.3F;
constexpr float kArrowColumn = 0.7F;
/* The title against retail's, grown with the frame. */
constexpr float kTitleScale = 1.4F;
/* Room left over so the wanted cards still fit after rounding. */
constexpr float kFitSlack = 0.05F;
/* The shortest frame against its width scale, so the art keeps its shape. */
constexpr float kMinHeight = 0.35F;

/* The frame's fixed geometry: its width scale, the tallest it may grow,
   and retail's insets inside the art. */
struct FrameFit {
  float scale_x = 0.0F;
  float max_z = 0.0F;
  float margin = 0.0F;
  float header = 0.0F;
  /* The art below the title's first line, unscaled. */
  float art = 0.0F;
};

bool fit_frame(const PopupTouchInput &input, FrameFit &out) {
  const SceneBox &native = input.frame;
  if (!(native.width() > 0.0F) || !(native.height() > 0.0F) ||
      !(input.line > 0.0F) || !(input.finger > 0.0F)) {
    return false;
  }
  const presentation::RetailScenePlane &plane = input.plane;
  out.margin = input.text_left - native.left;
  out.header = native.top - input.text_top;
  out.scale_x = std::min(kFrameFillWidth * plane.width() / native.width(),
                         kFrameFillHeight * plane.height() / native.height());
  out.max_z = kFrameFillHeight * plane.height() / native.height();
  out.art = native.height() - out.header - out.margin;
  return true;
}

/* What the frame holds below the title's first line besides the title. */
float below_title(const PopupTouchInput &input) {
  const float finger = input.finger;
  const float pitch = (kCardHeight + kCardGap) * finger;
  const int wanted = std::clamp(input.entries, 0, std::max(1, input.max_rows));
  return kCardGap * finger + static_cast<float>(wanted) * pitch +
         (input.footers > 0 ? (kButtonHeight + kCardGap) * finger : 0.0F) +
         kFitSlack * finger;
}

} // namespace

PopupTouchTitle popup_touch_title(const PopupTouchInput &input) {
  PopupTouchTitle out;
  FrameFit fit;
  if (!fit_frame(input, fit)) {
    return out;
  }
  out.width = fit.scale_x * (input.frame.width() - 2.0F * fit.margin);
  out.scale = kTitleScale * fit.scale_x;
  out.room = std::max(0.0F, fit.max_z * fit.art - below_title(input));
  return out;
}

PopupTouchLayout layout_popup_touch(const PopupTouchInput &input) {
  PopupTouchLayout out;
  FrameFit fit;
  if (!fit_frame(input, fit)) {
    return out;
  }
  const presentation::RetailScenePlane &plane = input.plane;
  const SceneBox &native = input.frame;
  const float finger = input.finger;
  const float scale_x = fit.scale_x;
  out.title_scale =
      input.title_scale > 0.0F ? input.title_scale : kTitleScale * scale_x;
  out.text_scale = kCardText * finger / input.line;
  const float line = input.line * out.title_scale;
  /* From the first line's middle down to the text's bottom. */
  const float title_below = std::max(input.title_height, line) - 0.5F * line;
  const float content = title_below + below_title(input);
  const float scale_z = std::clamp(content / std::max(fit.art, 1.0F),
                                   kMinHeight * scale_x, fit.max_z);
  out.frame_scale = {scale_x, scale_z};

  const float centre_x = plane.left() + 0.5F * plane.width();
  /* The frame stands on the screen's bottom edge, so the art's prompt strip
     below it is off the screen: the buttons are the frame's. */
  out.frame_offset = {centre_x - scale_x * 0.5F * (native.left + native.right),
                      -scale_z * native.bottom};
  out.frame = {out.frame_offset.x + scale_x * native.left,
               out.frame_offset.z + scale_z * native.bottom,
               out.frame_offset.x + scale_x * native.right,
               out.frame_offset.z + scale_z * native.top};
  const float inset = scale_x * fit.margin;
  out.title = {out.frame.left + inset, out.frame.top - scale_z * fit.header};

  const float body_left = out.frame.left + inset;
  const float body_right = out.frame.right - inset;
  const float body_bottom = out.frame.bottom + scale_z * fit.margin;
  out.text = {body_left, out.title.z - title_below, body_right,
              out.title.z + 0.5F * line};
  const float body_top = out.text.bottom - kCardGap * finger;
  const float card = kCardHeight * finger;
  const float gap = kCardGap * finger;

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
  const int rows = card_rows(body_top - list_bottom, finger, input.max_rows);
  const float card_right =
      input.entries > rows ? body_right - kArrowColumn * finger : body_right;
  out.cards =
      stack_cards({body_left, list_bottom, card_right, body_top}, finger,
                  input.entries, input.first_shown, input.max_rows);
  const float arrow_x = 0.5F * (card_right + body_right);
  const int shown = std::max(1, static_cast<int>(out.cards.cards.size()));
  out.arrow_up = {arrow_x, body_top - 0.5F * card};
  out.arrow_down = {arrow_x,
                    body_top - static_cast<float>(shown - 1) * out.cards.pitch -
                        0.5F * card};
  return out;
}

} // namespace x2::input
