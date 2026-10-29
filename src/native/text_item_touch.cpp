/*
 * CMenuItemText on touch (docs/touch-play.md). A `$MENU_*` prompt (a
 * footer) is drawn as a finger-tall button of the game's option art with the
 * prompt's words in the game's font, lit while held, in place of its keycap
 * line, clear of the other footers' buttons; a menu's tab keeps retail's draw
 * and gets a finger-sized target. A press on either is the pointer at the item
 * until it lifts (x2::input::TouchTargets), so the menu's own handlers act on
 * it as on a click.
 */
#include "text_item_touch.hpp"
#include "../input/menu_touch_gaps.hpp"
#include "../input/native_touch_menus.hpp"
#include "../input/text_touch_layout.hpp"
#include "../input/touch_runtime.h"
#include "../input/touch_targets.hpp"
#include "automap_touch.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_image_reader.hpp"
#include "guest_memory_view.hpp"
#include "guest_modules.h"
#include "menu_action.hpp"
#include "menu_item_memory.hpp"
#include "menu_scene.hpp"
#include "menu_text.hpp"
#include "options_touch.hpp"
#include "retail_menu_model.hpp"
#include "team_touch.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
/* Hints for the d-pad and the move stick, which touch scrolls by a drag. */
constexpr std::array<std::string_view, 2> kPadHints{"$DPAD_", "$MOVE_"};
/* The camera stick's pair, the automap's Rotate: two buttons a finger
   holds. */
constexpr std::string_view kTurnPair = "$CAMERA_LEFT$CAMERA_RIGHT";
constexpr std::array<std::string_view, 2> kTurnNames{"turn left", "turn right"};
/* Where a press on an item's area is delivered: the raw box its menu's
   onMouse tests, or the box its own onMouse (0x005bc1b0) tests. */
enum class Aim : std::uint8_t { raw_box, item_box };
struct FingerItems {
  std::string_view menu_class;
  std::string_view prefix;
  Aim aim = Aim::raw_box;
};
/* The tabs and rows a menu's onMouse finds by name and tests with their raw
   box: CMenuShop 0x005d3400, CMenuReviewPaths 0x005d04d0, CMenuDangerRoom
   0x005b5a90, CMenuWorldMap 0x005e7f10, CMenuMain 0x005c9320 (Quit),
   CMenuTeam 0x005e25c0; and the lobby rows the online menus' onMouse walk
   the focus to and accept on the focused one: CMenuOnline, CMenuGameOptions
   and CMenuPlayerGameOptions 0x005caa80, CMenuCampaignLobby 0x005b03b0,
   CMenuHost 0x005ba2b0, CMenuJoin 0x005c8da0. */
constexpr std::array<FingerItems, 26> kFingerItems{{
    {"CMenuShop", "shop_option0"},
    {"CMenuShop", "stash_option0"},
    {"CMenuReviewPaths", "option0"},
    {"CMenuDangerRoom", "option_text"},
    {"CMenuWorldMap", "option0"},
    {"CMenuWorldMap", "list01_0"},
    {"CMenuMain", "debug_text"},
    {"CMenuTeam", "detail_option0"},
    {"CMenuOnline", "text_playername"},
    {"CMenuOnline", "text_networkset"},
    {"CMenuOnline", "text_ready"},
    {"CMenuGameOptions", "text_gamename"},
    {"CMenuGameOptions", "text_difficulty"},
    {"CMenuGameOptions", "text_gametype"},
    {"CMenuGameOptions", "text_mapname"},
    {"CMenuGameOptions", "text_inviteslots"},
    {"CMenuPlayerGameOptions", "text_gamename"},
    {"CMenuPlayerGameOptions", "text_difficulty"},
    {"CMenuPlayerGameOptions", "text_mapname"},
    {"CMenuCampaignLobby", "text_hostgame"},
    {"CMenuCampaignLobby", "text_joingame"},
    {"CMenuHost", "text_startgame"},
    {"CMenuHost", "text_kickplayer"},
    {"CMenuHost", "text_leaveroom"},
    {"CMenuJoin", "text_readygame"},
    {"CMenuJoin", "text_leaveroom"},
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
/* Items that show their menu's desctext1 prompt on touch, where the menu has
   no footer item for it: CMenuOptionsController's empty description line on
   the page's bottom strip (options_controller.XMLB names desctext1 and has no
   desctext item). */
constexpr std::array<FingerItems, 1> kPromptHosts{{
    {"CMenuOptionsController", "description_text"},
}};
/* A prompt host's button, for GET /menu. */
constexpr std::string_view kPromptName = "page prompt";
/* A menu's footers are its items `desctext1` to `desctext9`. */
constexpr std::string_view kFooterName = "desctext";
constexpr int kFooterNames = 9;

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
    const bool prompt_host = listed(kPromptHosts, menu_class, name) != nullptr;
    const std::string text = prompt_host ? menu_prompt(RD32(self + item::kMenu))
                             : text_at == 0u
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
    if (x2::native::team_touch_hides(self, text)) {
      restore(cpu, self);
      return true;
    }
    const bool turn_pair = x2::native::text_is_turn_pair(menu_class, text);
    if (!turn_pair && !x2::native::text_has_touch_button(menu_class, text)) {
      return false;
    }
    const auto plane = x2::native::live_scene_plane();
    menu_scene::Vectors vectors;
    if (!plane || !vectors) {
      return false;
    }
    call.thiscall(exe_linked(item::kRefresh), self);
    detach_all(call, self);
    const bool hidden =
        (call.virtual_call(self, item::kSlotHidden) & 0xffu) != 0u;
    const std::uint32_t model = RD32(self + item::kModel);
    if (hidden || model == 0u || call.virtual_call(model, kModelReady) == 0u) {
      return true;
    }

    const float finger = x2::input::menu_finger_units(*plane);
    x2::input::TouchTargets &targets = x2::input::touch_targets();
    if (turn_pair) {
      const auto areas = x2::input::pair_buttons(text_box(self), finger);
      const std::array<std::string, 2> labels = turn_labels(text);
      int turn = 0;
      for (std::size_t side = 0; side < areas.size(); ++side) {
        targets.put({areas[side], hit_centre(self), x2::input::TouchPress::own,
                     kTurnNames[side]});
        const bool held = targets.pressed(areas[side]);
        if (held) {
          turn = side == 0u ? -1 : 1;
        }
        draw_button(call, vectors, self, side, areas[side], labels[side], held,
                    finger);
      }
      x2::native::automap_touch_turn(turn);
      return true;
    }
    const std::string label = x2::native::menu_token_label(text);
    const SceneBox area = prompt_host
                              ? prompt_area(call, self, label, finger)
                              : footer_area(call, self, menu_class, finger);
    const auto own = prompt_host ? x2::native::menu_token_action(text)
                                 : x2::native::touch_token_action(text);
    targets.put(
        {area, hit_centre(self),
         own ? x2::input::TouchPress::own : x2::input::TouchPress::click,
         prompt_host ? kPromptName : std::string_view()});
    if (own && targets.take_tap(area)) {
      x2::native::post_menu_action(*own);
    }
    draw_button(call, vectors, self, 0u, area, label, targets.pressed(area),
                finger);
    return true;
  }

  /* The button gone, when retail draws the item. */
  void restore(const CPU &cpu, std::uint32_t self) {
    if (!buttons_.contains(self)) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    detach_all(call, self);
  }

  void destroy(const CPU &cpu, std::uint32_t self) {
    names_.erase(self);
    bars_.erase(self);
    const auto found = buttons_.find(self);
    if (found == buttons_.end()) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    for (Button &button : found->second) {
      detach(call, button);
      menu_scene::release(call, button.plain);
      menu_scene::release(call, button.held);
    }
    buttons_.erase(found);
  }

private:
  /* One finger button of the option art over `area`, lit while held, with
     `label` in the item's font fitted inside it. */
  void draw_button(x2::guest::GuestCall &call, menu_scene::Vectors &vectors,
                   std::uint32_t self, std::size_t index, const SceneBox &area,
                   const std::string &label, bool held, float finger) {
    Button &button = button_for(call, vectors, self, index);
    const std::uint32_t art = held ? button.held : button.plain;
    menu_scene::stretch(
        call, vectors, art, held ? button.held_art : button.plain_art, area,
        static_cast<float>(s16(self + item::kDepth)) + kButtonDepth);
    menu_scene::show_phase(call, art, kSettled);
    menu_scene::attach(call, art);
    button.attached = art;

    const std::uint32_t box = RD32(self + item::kTextBox);
    const x2::guest::GuestBlock line(menu_text::kLineBytes);
    if (label.empty() || box == 0u || !line) {
      return;
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
  }

  /* The menu's desctext1 prompt as the game's bytes. */
  static std::string menu_prompt(std::uint32_t menu) {
    const x2::native::LiveGuestMemory memory;
    x2::native::GuestImageReader reader(memory, x2::native::guest_exe_base());
    std::string prompt;
    if (!reader.pool2_bytes(RD32(menu + x2::menu::kMenuDesctext), &prompt)) {
      prompt.clear();
    }
    return prompt;
  }

  /* A prompt host's button: as wide as the prompt's words at a button's
     text size and the text pad at each end, from the host's left. */
  static SceneBox prompt_area(x2::guest::GuestCall &call, std::uint32_t self,
                              const std::string &label, float finger) {
    SceneBox text = text_box(self);
    const std::uint32_t box = RD32(self + item::kTextBox);
    const x2::guest::GuestBlock line(menu_text::kLineBytes);
    if (box != 0u && line) {
      const float scale =
          x2::input::kCardText * finger / menu_text::line_height(call, box);
      const float words =
          menu_text::width(call, box, line.address(), label, scale);
      text.right =
          std::min(text.right, text.left + words + 2.0F * kTextPad * finger);
    }
    return x2::input::text_button(text, finger);
  }

  /* The button over `self` among the footers of its menu drawn as buttons:
     its text's, unless it would touch a neighbour's. */
  SceneBox footer_area(x2::guest::GuestCall &call, std::uint32_t self,
                       std::string_view menu_class, float finger) {
    std::vector<SceneBox> texts{text_box(self)};
    for (const std::uint32_t other :
         footers_of(call, RD32(self + item::kMenu))) {
      if (other != self && drawn_as_button(call, other, menu_class)) {
        texts.push_back(text_box(other));
      }
    }
    return x2::input::footer_buttons(texts, finger).front();
  }

  /* Whether this draw puts a button over the shown footer `self`. */
  static bool drawn_as_button(x2::guest::GuestCall &call, std::uint32_t self,
                              std::string_view menu_class) {
    if ((RD8(self + item::kFlags) & x2::menu::kItemHidden) != 0u ||
        (call.virtual_call(self, item::kSlotHidden) & 0xffu) != 0u) {
      return false;
    }
    const std::uint32_t model = RD32(self + item::kModel);
    if (model == 0u || call.virtual_call(model, kModelReady) == 0u) {
      return false;
    }
    const std::uint32_t text_at = call.virtual_call(self, item::kSlotText);
    if (text_at == 0u) {
      return false;
    }
    const std::string_view text =
        x2::guest::guest_string(text_at, menu_text::kLineBytes);
    return !x2::native::text_is_pad_hint(menu_class, text) &&
           !x2::native::team_touch_hides(self, text) &&
           x2::native::text_has_touch_button(menu_class, text);
  }

  /* The menu's footers, `desctext1` on, remembered per menu. */
  const std::vector<std::uint32_t> &footers_of(x2::guest::GuestCall &call,
                                               std::uint32_t menu) {
    Footers &footers = footers_[menu];
    std::vector<std::uint32_t> &found = footers.items;
    if (footers.vtable == RD32(menu) &&
        (found.empty() || RD32(found.front() + item::kMenu) == menu)) {
      return found;
    }
    footers.vtable = RD32(menu);
    found.clear();
    for (int n = 1; n <= kFooterNames; ++n) {
      const x2::guest::GuestText name(std::string(kFooterName) +
                                      std::to_string(n));
      const std::uint32_t at =
          name ? call.thiscall(x2::native::guest_exe_base() +
                                   item::kItemByNameRva,
                               menu, {name.address()})
               : 0u;
      if (at != 0u) {
        found.push_back(at);
      }
    }
    return found;
  }

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
        {area, aim == Aim::item_box ? hit_centre(self)
                                    : x2::native::text_item_raw_centre(self)});
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

  /* An item's button `index`: its prompt's one, or a pair's side. */
  Button &button_for(x2::guest::GuestCall &call, menu_scene::Vectors &vectors,
                     std::uint32_t self, std::size_t index) {
    Button &button = buttons_[self][index];
    if (button.plain != 0u) {
      return button;
    }
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

  void detach_all(x2::guest::GuestCall &call, std::uint32_t self) {
    const auto found = buttons_.find(self);
    if (found == buttons_.end()) {
      return;
    }
    for (Button &button : found->second) {
      detach(call, button);
    }
  }

  /* The turn pair's two buttons' words: the prompt's words without its
     tokens, an arrow toward each side, after any style escape before the
     tokens. */
  static std::array<std::string, 2> turn_labels(std::string_view text) {
    const std::size_t at = std::min(text.find(kTurnPair), text.size());
    const std::string style(text.substr(0, at));
    const std::string words =
        x2::native::prompt_label(text.substr(at), kTurnPair);
    return {style + "< " + words, style + words + " >"};
  }

  /* A menu's footer items, found while its vtable was this. */
  struct Footers {
    std::uint32_t vtable = 0u;
    std::vector<std::uint32_t> items;
  };

  std::unordered_map<std::uint32_t, std::array<Button, 2>> buttons_;
  std::unordered_map<std::uint32_t, Footers> footers_;
  std::unordered_map<std::uint32_t, std::string> names_;
  std::unordered_map<std::uint32_t, std::uint32_t> bars_;
};

TextItemTouch g_text_item_touch;

} // namespace

bool x2::native::text_is_pad_hint(std::string_view menu_class,
                                  std::string_view text) {
  return x2::input::menu_is_native_on_touch(menu_class) &&
         std::any_of(kPadHints.begin(), kPadHints.end(),
                     [&](std::string_view hint) {
                       return text.find(hint) != std::string_view::npos;
                     });
}

bool x2::native::text_is_turn_pair(std::string_view menu_class,
                                   std::string_view text) {
  return x2::input::menu_is_native_on_touch(menu_class) &&
         text.find(kTurnPair) != std::string_view::npos;
}

bool x2::native::text_has_touch_button(std::string_view menu_class,
                                       std::string_view text) {
  return x2::input::menu_is_native_on_touch(menu_class) &&
         text.find(kToken) != std::string_view::npos;
}

extern "C" void x2_override_005c61d0(CPU *cpu) {
  const std::uint32_t self = cpu->reg[kX86pEcx];
  x2::native::team_touch_text_draw(*cpu, self, on_touch());
  if (on_touch() && g_text_item_touch.draw(*cpu, self)) {
    cpu->reg[kX86pEsp] += 4u;
    return;
  }
  g_text_item_touch.restore(*cpu, self);
  x2::native::options_touch_restore(*cpu, self);
  x86_guest_body(cpu, "XMen2.exe", kTextDraw);
}

extern "C" void x2_override_005c6fb0(CPU *cpu) {
  x2::native::team_touch_text_destroy(*cpu, cpu->reg[kX86pEcx]);
  g_text_item_touch.destroy(*cpu, cpu->reg[kX86pEcx]);
  x2::native::options_touch_destroy(*cpu, cpu->reg[kX86pEcx]);
  x86_guest_body(cpu, "XMen2.exe", kTextDestroy);
}

x2::presentation::ScenePoint
x2::native::text_item_raw_centre(std::uint32_t self) {
  const auto height = static_cast<float>(s16(self + item::kBoxHeight));
  return {static_cast<float>(s16(self + item::kBoxLeft)) +
              0.5F * static_cast<float>(s16(self + item::kBoxWidth)),
          static_cast<float>(s16(self + item::kBoxTop)) -
              0.5F * (height + 1.0F)};
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
