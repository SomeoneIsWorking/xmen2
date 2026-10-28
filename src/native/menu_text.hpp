#ifndef X2_MENU_TEXT_HPP
#define X2_MENU_TEXT_HPP

#include "../input/touch_cards.hpp"
#include "guest_call.hpp"

#include <cstdint>
#include <string_view>

/* Text drawn by port code with a menu item's own text box and font, as the
   game's items draw theirs (docs/RE/menus.md). */
namespace x2::native::menu_text {

/* Guest bytes a line is fitted into. */
inline constexpr std::uint32_t kLineBytes = 0x200u;
/* The text box line's alignment: 1 centred, 2 right; 8 centres it in h. */
inline constexpr std::uint32_t kAlignCentre = 0x1u;
inline constexpr std::uint32_t kAlignRight = 0x2u;
inline constexpr std::uint32_t kAlignMiddle = 0x8u;

/* The height of a line of `box`'s font at scale 1. */
float line_height(guest::GuestCall &call, std::uint32_t box);

/* `text` copied to `out` (kLineBytes) and broken by the font's wrap at
   `width`; true when it stayed on one line. */
bool wrap(guest::GuestCall &call, std::uint32_t box, std::uint32_t out,
          std::string_view text, float width, float scale);
/* The first break in `out` replaced with "...". */
void cut_with_ellipsis(std::uint32_t out);
/* English cuts a long line with "..."; other languages are not cut. */
bool is_english(guest::GuestCall &call);

/* Text shrinks to fit down to this share of its size, found in this many
   halvings. */
inline constexpr float kSmallestText = 0.6F;
inline constexpr int kFitSteps = 6;

/* The largest scale from `high` down to kSmallestText of it at which
   `fits(scale)` holds; the smallest when none does. */
template <typename Fits> float fitted_scale(float high, Fits &&fits) {
  float low = kSmallestText * high;
  if (fits(high)) {
    return high;
  }
  if (!fits(low)) {
    return low;
  }
  for (int step = 0; step < kFitSteps; ++step) {
    const float middle = 0.5F * (low + high);
    (fits(middle) ? low : high) = middle;
  }
  return low;
}

/* The text box line (FUN_005eecf0) over `area` in the scene: `item` is the
   item whose box the text box belongs to, `align` its flags. */
void line(guest::GuestCall &call, std::uint32_t item, std::uint32_t box,
          const input::SceneBox &area, float scale, std::uint32_t align,
          std::uint32_t colour, std::uint32_t text);

} // namespace x2::native::menu_text

#endif
