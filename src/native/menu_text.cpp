#include "menu_text.hpp"

#include "guest_modules.h"
#include "menu_item_memory.hpp"
#include "menu_scene.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace x2::native::menu_text {
namespace {

namespace item = menu_item;

/* The text box's line (x, z, w, h, scale, flags, colour, text, -1), local
   to the item, z the bottom of h; and its font. */
constexpr std::uint32_t kTextLine = 0x005eecf0u;
constexpr std::uint32_t kTextFont = 0x005ee590u;
/* The font manager's widest line (text, scale, font) and wrap (width, text,
   scale, font, 1), which breaks only at a space, both on x87, and "A". */
constexpr std::uint32_t kFonts = 0x00597c20u;
constexpr std::uint32_t kWidth = 0x34u;
constexpr std::uint32_t kWrap = 0x38u;
constexpr std::uint32_t kLineProbe = 0x0069a444u;
/* The language object; vt+0x18 its code, "e..." for English. */
constexpr std::uint32_t kLanguage = 0x0058a3a0u;
constexpr std::uint32_t kLanguageCode = 0x18u;
constexpr float kWideLine = 10000.0F;

int s16(std::uint32_t address) {
  return static_cast<std::int16_t>(RD16(address));
}

std::uint32_t word(float value) {
  return static_cast<std::uint32_t>(static_cast<std::int32_t>(value));
}

void copy_line(std::uint32_t out, std::string_view text) {
  const std::size_t length =
      std::min<std::size_t>(text.size(), kLineBytes - 4u);
  std::memcpy(guest_memory_as<char>(out), text.data(), length);
  WR8(out + static_cast<std::uint32_t>(length), 0u);
}

float widest_line(guest::GuestCall &call, std::uint32_t box, std::uint32_t text,
                  float scale) {
  const std::uint32_t font = call.thiscall(exe_linked(kTextFont), box);
  const std::uint32_t fonts = call.cdecl_call(exe_linked(kFonts));
  call.virtual_call(fonts, kWidth, {text, guest::float_bits(scale), font});
  return static_cast<float>(call.x87_result());
}

} // namespace

float wrap_in_place(guest::GuestCall &call, std::uint32_t box,
                    std::uint32_t text, float width, float scale) {
  const std::uint32_t font = call.thiscall(exe_linked(kTextFont), box);
  const std::uint32_t fonts = call.cdecl_call(exe_linked(kFonts));
  call.virtual_call(fonts, kWrap,
                    {word(width), text, guest::float_bits(scale), font, 1u});
  return static_cast<float>(call.x87_result());
}

float width(guest::GuestCall &call, std::uint32_t box, std::uint32_t out,
            std::string_view text, float scale) {
  copy_line(out, text);
  return widest_line(call, box, out, scale);
}

float line_height(guest::GuestCall &call, std::uint32_t box) {
  return wrap_in_place(call, box, exe_linked(kLineProbe), kWideLine, 1.0F);
}

bool wrap(guest::GuestCall &call, std::uint32_t box, std::uint32_t out,
          std::string_view text, float width, float scale) {
  copy_line(out, text);
  wrap_in_place(call, box, out, std::ceil(width), scale);
  return guest::guest_string(out, kLineBytes).find('\n') ==
             std::string_view::npos &&
         widest_line(call, box, out, scale) <= width;
}

void cut_with_ellipsis(std::uint32_t out) {
  const std::size_t cut = guest::guest_string(out, kLineBytes).find('\n');
  if (cut != std::string_view::npos && cut + 4u < kLineBytes) {
    std::memcpy(guest_memory_as<char>(out + static_cast<std::uint32_t>(cut)),
                "...", 4u);
  }
}

bool is_english(guest::GuestCall &call) {
  const std::uint32_t language = call.cdecl_call(exe_linked(kLanguage));
  const std::uint32_t code = call.virtual_call(language, kLanguageCode);
  return code != 0u && RD8(code) == static_cast<std::uint8_t>('e');
}

void line(guest::GuestCall &call, std::uint32_t item, std::uint32_t box,
          const input::SceneBox &area, float scale, std::uint32_t align,
          std::uint32_t colour, std::uint32_t text) {
  const auto origin_x = static_cast<float>(s16(item + item::kBoxLeft) -
                                           s16(item + item::kLocalLeft));
  const auto origin_z = static_cast<float>(s16(item + item::kBoxTop) -
                                           s16(item + item::kLocalTop));
  call.thiscall(exe_linked(kTextLine), box,
                {word(area.left - origin_x), word(area.bottom - origin_z),
                 word(area.width()), word(area.height()),
                 guest::float_bits(scale), align, colour, text, 0xffffffffu});
}

void label_and_value(guest::GuestCall &call, std::uint32_t host,
                     std::uint32_t self, const input::SceneBox &card,
                     std::string_view label, std::string_view value,
                     bool focused, float pad, float label_share) {
  const std::uint32_t box = RD32(host + menu_item::kTextBox);
  const guest::GuestBlock line_text(kLineBytes);
  menu_scene::Vectors vectors;
  if (box == 0u || !line_text || !vectors) {
    return;
  }
  const input::SceneBox inner = input::card_text_box(card, pad * card.height());
  const float split = inner.left + label_share * inner.width();
  const input::SceneBox left{inner.left, inner.bottom, split, inner.top};
  const input::SceneBox right{split, inner.bottom, inner.right, inner.top};
  const float scale = row_scale([&](float s) {
    return wrap(call, box, line_text.address(), label, left.width(), s) &&
           wrap(call, box, line_text.address(), value, right.width(), s);
  });
  const std::uint32_t colour =
      menu_scene::style_color(call, vectors, RD32(self + menu_item::kStyle),
                              focused ? 1.0F : 0.0F, true, self);
  wrap(call, box, line_text.address(), label, left.width(), scale);
  line(call, host, box, left, scale, kAlignMiddle, colour, line_text.address());
  wrap(call, box, line_text.address(), value, right.width(), scale);
  line(call, host, box, right, scale, kAlignRight | kAlignMiddle, colour,
       line_text.address());
}

} // namespace x2::native::menu_text
