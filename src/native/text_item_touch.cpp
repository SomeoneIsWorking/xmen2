/*
 * CMenuItemText on touch (docs/touch-play.md). A `$MENU_*` prompt (a
 * footer) is drawn as a finger-tall button of the game's option art with the
 * prompt's words in the game's font, lit while held, in place of its keycap
 * line; a menu's tab keeps retail's draw and gets a finger-sized target. A
 * press on either is the pointer at the item until it lifts
 * (x2::input::TouchTargets), so the menu's own handlers act on it as on a
 * click.
 */
#include "text_item_touch.hpp"
#include "../input/menu_touch_gaps.hpp"
#include "../input/native_touch_menus.hpp"
#include "../input/text_touch_layout.hpp"
#include "../input/touch_runtime.h"
#include "../input/touch_targets.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_action.hpp"
#include "menu_item_memory.hpp"
#include "menu_scene.hpp"
#include "menu_text.hpp"
#include "options_touch.hpp"
#include "retail_menu_model.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

namespace item = x2::native::menu_item;
namespace menu_scene = x2::native::menu_scene;
namespace menu_text = x2::native::menu_text;
using x2::input::SceneBox;
using x2::native::exe_linked;

/* CMenuItemText's draw (vtable 0x006a10cc +0x20) and the destructor its
   scalar destructor runs. */
constexpr std::uint32_t kTextDraw = 0x005c61d0u;
constexpr std::uint32_t kTextDestroy = 0x005c6fb0u;
/* The model's ready check (vt+0x18). */
constexpr std::uint32_t kModelReady = 0x18u;
/* The text's lit share, used while the style's +0x04 is above the
   threshold at 0x00680030; the focus-lit bit otherwise. */
constexpr std::uint32_t kLit = 0x98u;
constexpr std::uint32_t kStyleFade = 0x04u;
constexpr std::uint32_t kStyleFadeThreshold = 0x00680030u;

constexpr std::string_view kToken = "$MENU_";
constexpr std::string_view kPadHint = "$DPAD_";
/* The tabs and rows a menu's onMouse finds by name and tests with their raw
   box: CMenuShop 0x005d3400, CMenuReviewPaths 0x005d04d0, CMenuDangerRoom
   0x005b5a90, CMenuWorldMap 0x005e7f10, CMenuMain 0x005c9320 (Quit). */
/* Where a press on an item's area is delivered: the raw box its menu's
   onMouse tests, or the box its own onMouse (0x005bc1b0) tests. */
enum class Aim : std::uint8_t { raw_box, item_box };
struct FingerItems {
  std::string_view menu_class;
  std::string_view prefix;
  Aim aim = Aim::raw_box;
};
constexpr std::array<FingerItems, 7> kFingerItems{{
    {"CMenuShop", "shop_option0"},
    {"CMenuShop", "stash_option0"},
    {"CMenuReviewPaths", "option0"},
    {"CMenuDangerRoom", "option_text"},
    {"CMenuWorldMap", "option0"},
    {"CMenuWorldMap", "list01_0"},
    {"CMenuMain", "debug_text"},
}};
/* Rows drawn on a bar, the item named as the row without this prefix: those
   CMenuMain 0x005c9320 and CMenuPDA 0x005cc1f0 test by raw box, and
   Options' Accept, which its item's onMouse tests. */
constexpr std::array<FingerItems, 3> kBarRows{{
    {"CMenuMain", "label_option0"},
    {"CMenuPDA", "label_option0"},
    {"CMenuOptions", "label_accept", Aim::item_box},
}};
constexpr std::string_view kRowPrefix = "label_";

template <std::size_t N>
const FingerItems *listed(const std::array<FingerItems, N> &items,
                          std::string_view menu_class, std::string_view name) {
  const auto found =
      std::find_if(items.begin(), items.end(), [&](const FingerItems &at) {
        return menu_class == at.menu_class && name.starts_with(at.prefix);
      });
  return found == items.end() ? nullptr : &*found;
}
/* The game's option art: dark for a button, lit while it is held. */
constexpr std::string_view kButtonArt = "ui/models/m_pda_option_screen";
constexpr std::string_view kHeldArt = "ui/models/m_pda_option_focus";
/* The art's animations play it in; it is shown done. */
constexpr float kSettled = 0.999F;
/* Buttons sit this much behind the text. */
constexpr float kButtonDepth = 5.0F;
/* Text inset from a button's ends, in fingers. */
constexpr float kTextPad = 0.15F;

int s16(std::uint32_t address) {
  return static_cast<std::int16_t>(RD16(address));
}

bool on_touch() { return x2::input::touch_runtime_active() != 0; }

/* Where the draw puts the item's text line, z up. */
SceneBox text_box(std::uint32_t self) {
  const auto left = static_cast<float>(s16(self + item::kBoxLeft));
  const auto top = static_cast<float>(s16(self + item::kBoxTop));
  return {left, top - static_cast<float>(s16(self + item::kBoxHeight)),
          left + static_cast<float>(s16(self + item::kBoxWidth)), top};
}

/* The colour share the retail draw lights the item's text by. */
float retail_lit(std::uint32_t self) {
  const std::uint32_t style = RD32(self + item::kStyle);
  if (style != 0u &&
      RDF32(style + kStyleFade) > RDF32(exe_linked(kStyleFadeThreshold))) {
    return RDF32(self + kLit);
  }
  return (RD8(self + item::kFlags) & x2::menu::kItemFocusLit) != 0u ? 1.0F
                                                                    : 0.0F;
}

/* The middle of the raw box a menu's onMouse tests a tab with. */
x2::presentation::ScenePoint raw_centre(std::uint32_t self) {
  const auto height = static_cast<float>(s16(self + item::kBoxHeight));
  return {static_cast<float>(s16(self + item::kBoxLeft)) +
              0.5F * static_cast<float>(s16(self + item::kBoxWidth)),
          static_cast<float>(s16(self + item::kBoxTop)) -
              0.5F * (height + 1.0F)};
}

/* The middle of the box the item's onMouse (0x005c6490) hit-tests. */
x2::presentation::ScenePoint hit_centre(std::uint32_t self) {
  const x2::menu::SceneRect hit = x2::menu::item_hit_rect(
      s16(self + item::kBoxLeft), s16(self + item::kBoxTop),
      s16(self + item::kBoxWidth), s16(self + item::kBoxHeight),
      s16(self + item::kLocalTop));
  return {0.5F * static_cast<float>(hit.left + hit.right),
          0.5F * static_cast<float>(hit.top + hit.bottom)};
}

class TextItemTouch {
public:
  /* The touch draw; false leaves the item to retail's. */
  bool draw(const CPU &cpu, std::uint32_t self) {
    const std::uint32_t image = x2::native::guest_exe_base();
    /* CMenuItemListItems' draw (0x005c4250) is a jump here, and
       CMenuItemBinary's vtable (0x006a04b4) names this draw. */
    const auto kind = x2::menu::classify_item(RD32(self), image);
    if (kind != x2::menu::ItemClass::text &&
        kind != x2::menu::ItemClass::list_items &&
        kind != x2::menu::ItemClass::binary) {
      return false;
    }
    const std::string_view menu_class =
        x2::menu::classify_menu(RD32(RD32(self + item::kMenu)), image);
    if (!x2::input::menu_is_native_on_touch(menu_class)) {
      return false;
    }
    const std::string &name = name_of(self);
    if (x2::native::options_touch_draw(cpu, self, menu_class, name)) {
      return true;
    }
    if (kind == x2::menu::ItemClass::binary) {
      return false;
    }
    x2::guest::GuestCall call(cpu);
    const std::uint32_t text_at = call.virtual_call(self, item::kSlotText);
    const std::string text = text_at == 0u
                                 ? std::string()
                                 : std::string(x2::guest::guest_string(
                                       text_at, menu_text::kLineBytes));
    const FingerItems *bar_row = listed(kBarRows, menu_class, name);
    const FingerItems *finger_item =
        bar_row != nullptr ? bar_row : listed(kFingerItems, menu_class, name);
    if (finger_item != nullptr) {
      if (!text.empty()) {
        put_finger_area(call, self,
                        bar_row != nullptr ? bar_of(call, self, name) : 0u,
                        finger_item->aim);
      }
      return false;
    }
    if (x2::native::text_is_pad_hint(menu_class, text)) {
      return true;
    }
    if (!x2::native::text_has_touch_button(menu_class, text)) {
      return false;
    }
    const auto plane = x2::native::live_scene_plane();
    menu_scene::Vectors vectors;
    if (!plane || !vectors) {
      return false;
    }
    Button &button = button_for(call, vectors, self);
    call.thiscall(exe_linked(item::kRefresh), self);
    detach(call, button);
    const bool hidden =
        (call.virtual_call(self, item::kSlotHidden) & 0xffu) != 0u;
    const std::uint32_t model = RD32(self + item::kModel);
    if (hidden || model == 0u || call.virtual_call(model, kModelReady) == 0u) {
      return true;
    }

    const float finger = x2::input::menu_finger_units(*plane);
    const SceneBox area = x2::input::text_button(text_box(self), finger);
    x2::input::TouchTargets &targets = x2::input::touch_targets();
    targets.put({area, hit_centre(self)});
    const bool held = targets.pressed(area);

    const std::uint32_t art = held ? button.held : button.plain;
    menu_scene::stretch(
        call, vectors, art, held ? button.held_art : button.plain_art, area,
        static_cast<float>(s16(self + item::kDepth)) + kButtonDepth);
    menu_scene::show_phase(call, art, kSettled);
    menu_scene::attach(call, art);
    button.attached = art;

    const std::string label = x2::native::menu_token_label(text);
    const std::uint32_t box = RD32(self + item::kTextBox);
    const x2::guest::GuestBlock line(menu_text::kLineBytes);
    if (label.empty() || box == 0u || !line) {
      return true;
    }
    const float pad = kTextPad * finger;
    const SceneBox words{area.left + pad, area.bottom, area.right - pad,
                         area.top};
    if (label != button.fitted_for || finger != button.fitted_finger) {
      const float high =
          x2::input::kCardText * finger / menu_text::line_height(call, box);
      button.scale = menu_text::fitted_scale(high, [&](float scale) {
        return menu_text::wrap(call, box, line.address(), label, words.width(),
                               scale);
      });
      button.fitted_for = label;
      button.fitted_finger = finger;
    }
    if (!menu_text::wrap(call, box, line.address(), label, words.width(),
                         button.scale) &&
        menu_text::is_english(call)) {
      menu_text::cut_with_ellipsis(line.address());
    }
    const std::uint8_t flags = RD8(self + item::kFlags);
    const std::uint32_t colour =
        menu_scene::style_color(call, vectors, RD32(self + item::kStyle),
                                held ? 1.0F : retail_lit(self),
                                (flags & x2::menu::kItemEnabled) != 0u, self);
    menu_text::line(call, self, box, words, button.scale,
                    menu_text::kAlignCentre | menu_text::kAlignMiddle, colour,
                    line.address());
    return true;
  }

  /* The button gone, when retail draws the item. */
  void restore(const CPU &cpu, std::uint32_t self) {
    const auto found = buttons_.find(self);
    if (found == buttons_.end() || found->second.attached == 0u) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    detach(call, found->second);
  }

  void destroy(const CPU &cpu, std::uint32_t self) {
    names_.erase(self);
    bars_.erase(self);
    const auto found = buttons_.find(self);
    if (found == buttons_.end()) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    detach(call, found->second);
    menu_scene::release(call, found->second.plain);
    menu_scene::release(call, found->second.held);
    buttons_.erase(found);
  }

private:
  /* A shown, enabled tab or row's target: the finger button over its text,
     across its bar when it has one. */
  static void put_finger_area(x2::guest::GuestCall &call, std::uint32_t self,
                              std::uint32_t bar, Aim aim) {
    const auto plane = x2::native::live_scene_plane();
    const std::uint8_t flags = RD8(self + item::kFlags);
    if (!plane || (flags & x2::menu::kItemHidden) != 0u ||
        (flags & x2::menu::kItemEnabled) == 0u ||
        (call.virtual_call(self, item::kSlotHidden) & 0xffu) != 0u) {
      return;
    }
    const float finger = x2::input::menu_finger_units(*plane);
    SceneBox area = x2::input::text_button(text_box(self), finger);
    if (bar != 0u) {
      const auto left = static_cast<float>(s16(bar + item::kBoxLeft));
      area = x2::input::row_button(
          text_box(self), left,
          left + static_cast<float>(s16(bar + item::kBoxWidth)), finger);
    }
    x2::input::touch_targets().put(
        {area, aim == Aim::item_box ? hit_centre(self) : raw_centre(self)});
  }

  const std::string &name_of(std::uint32_t self) {
    const auto found = names_.find(self);
    if (found != names_.end()) {
      return found->second;
    }
    return names_[self] = x2::menu::read_live_item_name(self);
  }

  /* The bar model a row is drawn on (its x box is the art's; its z box is
     never updated), or 0. */
  std::uint32_t bar_of(x2::guest::GuestCall &call, std::uint32_t self,
                       std::string_view name) {
    const auto found = bars_.find(self);
    if (found != bars_.end()) {
      return found->second;
    }
    const x2::guest::GuestText bar_name(name.substr(kRowPrefix.size()));
    std::uint32_t bar = 0u;
    if (name.starts_with(kRowPrefix) && bar_name) {
      bar = call.thiscall(x2::native::guest_exe_base() + item::kItemByNameRva,
                          RD32(self + item::kMenu), {bar_name.address()});
    }
    return bars_[self] = bar;
  }

  struct Button {
    std::uint32_t plain = 0u;
    std::uint32_t held = 0u;
    SceneBox plain_art;
    SceneBox held_art;
    std::uint32_t attached = 0u;
    /* The text scale the label fits at, and the label and finger it was
       found for. */
    float scale = 1.0F;
    std::string fitted_for;
    float fitted_finger = 0.0F;
  };

  Button &button_for(x2::guest::GuestCall &call, menu_scene::Vectors &vectors,
                     std::uint32_t self) {
    const auto found = buttons_.find(self);
    if (found != buttons_.end()) {
      return found->second;
    }
    Button &button = buttons_[self];
    button.plain = menu_scene::load_model(call, kButtonArt);
    button.held = menu_scene::load_model(call, kHeldArt);
    button.plain_art = menu_scene::bounds(call, vectors, button.plain);
    button.held_art = menu_scene::bounds(call, vectors, button.held);
    menu_scene::stretch(call, vectors, button.plain, {}, {}, 0.0F);
    menu_scene::stretch(call, vectors, button.held, {}, {}, 0.0F);
    return button;
  }

  static void detach(x2::guest::GuestCall &call, Button &button) {
    if (button.attached != 0u) {
      menu_scene::detach(call, button.attached);
      button.attached = 0u;
    }
  }

  std::unordered_map<std::uint32_t, Button> buttons_;
  std::unordered_map<std::uint32_t, std::string> names_;
  std::unordered_map<std::uint32_t, std::uint32_t> bars_;
};

TextItemTouch g_text_item_touch;

} // namespace

bool x2::native::text_is_pad_hint(std::string_view menu_class,
                                  std::string_view text) {
  return x2::input::menu_is_native_on_touch(menu_class) &&
         text.find(kPadHint) != std::string_view::npos;
}

bool x2::native::text_has_touch_button(std::string_view menu_class,
                                       std::string_view text) {
  return x2::input::menu_is_native_on_touch(menu_class) &&
         text.find(kToken) != std::string_view::npos;
}

extern "C" void x2_override_005c61d0(CPU *cpu) {
  const std::uint32_t self = cpu->reg[kX86pEcx];
  if (on_touch() && g_text_item_touch.draw(*cpu, self)) {
    cpu->reg[kX86pEsp] += 4u;
    return;
  }
  g_text_item_touch.restore(*cpu, self);
  x2::native::options_touch_restore(*cpu, self);
  x86_guest_body(cpu, "XMen2.exe", kTextDraw);
}

extern "C" void x2_override_005c6fb0(CPU *cpu) {
  g_text_item_touch.destroy(*cpu, cpu->reg[kX86pEcx]);
  x2::native::options_touch_destroy(*cpu, cpu->reg[kX86pEcx]);
  x86_guest_body(cpu, "XMen2.exe", kTextDestroy);
}

bool x2::native::text_has_finger_area(std::string_view menu_class,
                                      std::string_view name) {
  return listed(kFingerItems, menu_class, name) != nullptr ||
         listed(kBarRows, menu_class, name) != nullptr;
}

__attribute__((constructor)) static void x2_text_item_touch_register(void) {
  x86_register_override("XMen2.exe", kTextDraw, x2_override_005c61d0);
  x86_register_override("XMen2.exe", kTextDestroy, x2_override_005c6fb0);
}
