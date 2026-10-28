/*
 * CMenuOptions on touch (docs/touch-play.md): its nine settings as a column
 * of finger-tall cards of the game's option art between the title bar and
 * the Accept bar, the focused one lit, each with its label and value in the
 * game's font and a volume card with the game's own bar; the panel art and
 * the rows' focus bars, laid out for the retail rows, are hidden. A drag
 * scrolls the column, a tap on a setting is the retail click on its row and
 * a tap or a slide along a volume bar steps it toward the finger through
 * the menu's own handler (0x005cb5f0).
 */
#include "options_touch.hpp"
#include "../input/menu_touch_gaps.hpp"
#include "../input/options_touch_layout.hpp"
#include "../input/touch_runtime.h"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_item_memory.hpp"
#include "menu_scene.hpp"
#include "menu_text.hpp"
#include "retail_menu_model.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"
#include "x86rt_native.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
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
using x2::presentation::ScenePoint;

/* CMenuItemBar's draw (vtable 0x006a042c +0x20) and CMenuOptions::onMouse
   (vtable 0x0069ebd4 +0x80): (x, z, _, message, wheel), ret 0x14. */
constexpr std::uint32_t kBarDraw = 0x005bdac0u;
constexpr std::uint32_t kOnMouse = 0x005cb5f0u;

/* Options' page 0 is the settings, 1 and 2 the controls. */
constexpr std::uint32_t kPage = x2::menu::kMenuPage;
/* CMenuItemBar+0x40, its level from 0 to 1. */
constexpr std::uint32_t kBarFill = 0x40u;

constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kPointerMove = 0x200u;
constexpr std::uint32_t kButtonHeld = 0x0001u;

/* The settings in the order the panel shows them: each one's label (the
   item onMouse tests), the item that shows its value and its focus bar. */
struct Setting {
  std::string_view label;
  std::string_view value;
  std::string_view focus;
  bool volume;
};
constexpr std::array<Setting, 9> kSettings{{
    {"label_effects_volume", "fx_vol", "options_fxvol_focus", true},
    {"label_music_volume", "music_vol", "options_musicvol_focus", true},
    {"label_combat_music", "combat_music", "options_combatmusic_focus", false},
    {"label_view_angle", "view_angle", "view_angle_focus", false},
    {"label_view_cycle", "view_cycle", "view_cycle_focus", false},
    {"label_view_follow", "view_follow", "view_follow_focus", false},
    {"label_view_shake", "view_shake", "view_shake_focus", false},
    {"label_subtitles", "subtitles", "subtitles_focus", false},
    {"label_vibration", "vibration", "options_vibration_focus", false},
}};
constexpr std::string_view kPanelArt = "options_screen";
constexpr std::string_view kTitle = "label_options";
constexpr std::string_view kTitleBar = "title";
constexpr std::string_view kAccept = "label_accept";

/* The game's option art: dark for a setting, lit for the focused one. */
constexpr std::string_view kCardArt = "ui/models/m_pda_option_screen";
constexpr std::string_view kFocusArt = "ui/models/m_pda_option_focus";
/* The art's animations play it in; it is shown done. */
constexpr float kSettled = 0.999F;
/* Cards sit this much behind the text, a volume's slot this much behind its
   bar and this much past it. */
constexpr float kCardDepth = 5.0F;
constexpr float kTrackDepth = 2.0F;
constexpr float kTrackFrame = 2.0F;
/* Text inset from a card's ends, and the label's share of a setting card,
   in fingers and of the text width. */
constexpr float kTextPad = 0.25F;
constexpr float kLabelShare = 0.62F;
/* A drag scrolls after this share of a card pitch, a slide along a volume
   bar after this share of a finger. */
constexpr float kDragStart = 0.25F;
/* Retail steps a bar a tenth (setincrement); it is at the finger within
   half a step. A step not seen in this many draws is given up. */
constexpr float kHalfStep = 0.05F;
constexpr int kStepDraws = 30;

int s16(std::uint32_t address) {
  return static_cast<std::int16_t>(RD16(address));
}

std::uint32_t word(float value) {
  return static_cast<std::uint32_t>(
      static_cast<std::int32_t>(std::lround(value)));
}

bool on_touch() { return x2::input::touch_runtime_active() != 0; }

SceneBox text_box(std::uint32_t self) {
  const auto left = static_cast<float>(s16(self + item::kBoxLeft));
  const auto top = static_cast<float>(s16(self + item::kBoxTop));
  return {left, top - static_cast<float>(s16(self + item::kBoxHeight)),
          left + static_cast<float>(s16(self + item::kBoxWidth)), top};
}

std::uint32_t item_named(x2::guest::GuestCall &call, std::uint32_t menu,
                         std::string_view name) {
  const x2::guest::GuestText text(name);
  if (!text) {
    return 0u;
  }
  return call.thiscall(x2::native::guest_exe_base() + item::kItemByNameRva,
                       menu, {text.address()});
}

std::string item_text(x2::guest::GuestCall &call, std::uint32_t self) {
  const std::uint32_t at = call.virtual_call(self, item::kSlotText);
  return at == 0u
             ? std::string()
             : std::string(x2::guest::guest_string(at, menu_text::kLineBytes));
}

class OptionsTouch {
public:
  bool draw(const CPU &cpu, std::uint32_t self, std::string_view name) {
    const auto setting = setting_of(name);
    const bool value = !setting && value_of(name).has_value();
    if (!setting && !value) {
      return false;
    }
    const std::uint32_t menu = RD32(self + item::kMenu);
    x2::guest::GuestCall call(cpu);
    Panel &panel = panel_for(call, menu);
    if (RD32(menu + kPage) != 0u) {
      show_retail(call, panel);
      return false;
    }
    /* Retail's draw refreshes the item's text from its gamevar first. */
    call.thiscall(exe_linked(item::kRefresh), self);
    if (value) {
      return true;
    }
    const auto plane = x2::native::live_scene_plane();
    menu_scene::Vectors vectors;
    if (!plane || !vectors || panel.title == 0u || panel.accept == 0u ||
        panel.title_bar == 0u) {
      return false;
    }
    hide_retail(call, panel);
    const float finger = x2::input::menu_finger_units(*plane);
    const SceneBox title = text_box(panel.title);
    const SceneBox accept = text_box(panel.accept);
    const auto bar_left =
        static_cast<float>(s16(panel.title_bar + item::kBoxLeft));
    x2::input::OptionsTouchInput input;
    input.title_z = 0.5F * (title.bottom + title.top);
    input.accept_z = 0.5F * (accept.bottom + accept.top);
    input.left = bar_left;
    input.right =
        bar_left + static_cast<float>(s16(panel.title_bar + item::kBoxWidth));
    input.finger = finger;
    input.rows = static_cast<int>(kSettings.size());
    input.first = panel.first;
    panel.stack = x2::input::layout_options_touch(input);
    panel.first = panel.stack.first;
    panel.finger = finger;
    panel.drawn = true;

    const auto index = static_cast<std::size_t>(*setting);
    Row &row = panel.rows[index];
    detach(call, row);
    const int shown = *setting - panel.stack.first;
    row.shown = shown >= 0 &&
                shown < static_cast<int>(panel.stack.cards.size()) &&
                (call.virtual_call(self, item::kSlotHidden) & 0xffu) == 0u;
    if (!row.shown) {
      return true;
    }
    row.card = panel.stack.cards[static_cast<std::size_t>(shown)];
    const bool focused = RD32(menu + x2::menu::kMenuFocus) == self;
    const float depth = static_cast<float>(s16(self + item::kDepth));
    const std::uint32_t art = focused ? row.lit : row.plain;
    menu_scene::stretch(call, vectors, art,
                        focused ? panel.lit_art : panel.plain_art, row.card,
                        depth + kCardDepth);
    menu_scene::show_phase(call, art, kSettled);
    menu_scene::attach(call, art);
    row.attached = art;
    if (kSettings[index].volume && row.value != 0u && row.track != 0u) {
      row.bar = x2::input::volume_bar(
          row.card, kTextPad * finger,
          static_cast<float>(s16(row.value + item::kBoxHeight)));
      const SceneBox slot{
          row.bar.left - kTrackFrame, row.bar.bottom - kTrackFrame,
          row.bar.right + kTrackFrame, row.bar.top + kTrackFrame};
      menu_scene::stretch(call, vectors, row.track, panel.plain_art, slot,
                          static_cast<float>(s16(row.value + item::kDepth)) +
                              kTrackDepth);
      menu_scene::show_phase(call, row.track, kSettled);
      menu_scene::attach(call, row.track);
      row.track_attached = true;
    }
    draw_text(call, vectors, panel, row, self, kSettings[index], focused);
    return true;
  }

  void restore(const CPU &cpu, std::uint32_t self) {
    const std::uint32_t menu = RD32(self + item::kMenu);
    const auto found = panels_.find(menu);
    if (found == panels_.end()) {
      return;
    }
    const auto &rows = found->second.rows;
    if (std::none_of(rows.begin(), rows.end(),
                     [&](const Row &row) { return row.label == self; })) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    show_retail(call, found->second);
  }

  void destroy(const CPU &cpu, std::uint32_t self) {
    for (auto at = panels_.begin(); at != panels_.end(); ++at) {
      Panel &panel = at->second;
      if (panel.rows[0].label != self) {
        continue;
      }
      x2::guest::GuestCall call(cpu);
      for (Row &row : panel.rows) {
        detach(call, row);
        menu_scene::release(call, row.plain);
        menu_scene::release(call, row.lit);
        if (row.track != 0u) {
          menu_scene::release(call, row.track);
        }
      }
      panels_.erase(at);
      return;
    }
  }

  /* A bar's draw: in its card on touch, retail's otherwise. */
  void draw_bar(CPU *cpu, std::uint32_t self) {
    Panel *panel = drawn_panel(RD32(self + item::kMenu));
    const auto setting =
        panel != nullptr ? setting_of_bar(*panel, self) : std::nullopt;
    if (!on_touch() || !setting) {
      x86_guest_body(cpu, "XMen2.exe", kBarDraw);
      return;
    }
    Row &row = panel->rows[static_cast<std::size_t>(*setting)];
    row.retail_bar = text_box(self);
    step_toward_finger(*cpu, row, self);
    if (!row.shown) {
      cpu->reg[kX86pEsp] += 4u;
      return;
    }
    const std::uint16_t left = RD16(self + item::kBoxLeft);
    const std::uint16_t top = RD16(self + item::kBoxTop);
    const std::uint16_t width = RD16(self + item::kBoxWidth);
    WR16(self + item::kBoxLeft, static_cast<std::uint16_t>(word(row.bar.left)));
    WR16(self + item::kBoxTop, static_cast<std::uint16_t>(word(row.bar.top)));
    WR16(self + item::kBoxWidth,
         static_cast<std::uint16_t>(word(row.bar.width())));
    x86_guest_body(cpu, "XMen2.exe", kBarDraw);
    WR16(self + item::kBoxLeft, left);
    WR16(self + item::kBoxTop, top);
    WR16(self + item::kBoxWidth, width);
  }

  /* The finger on CMenuOptions: true when touch took the message. */
  bool pointer(const CPU &cpu, std::uint32_t menu, ScenePoint point,
               std::uint32_t message) {
    if (forwarding_ || !on_touch()) {
      return false;
    }
    Panel *panel = drawn_panel(menu);
    if (panel == nullptr || RD32(menu + kPage) != 0u) {
      return false;
    }
    const x2::input::CardStack &stack = panel->stack;
    const int card = x2::input::box_hit(stack.cards, point);
    if (message == kButtonDown) {
      press_ = {};
      if (card < 0) {
        return false;
      }
      press_ = {menu,  true,        Gesture::tap,
                point, stack.first, stack.first + card};
      return true;
    }
    if (!press_.held || press_.menu != menu) {
      /* A finger does not hover: retail would focus the row under it. */
      return message == kPointerMove && card >= 0;
    }
    Row &row = panel->rows[static_cast<std::size_t>(press_.setting)];
    const bool volume =
        kSettings[static_cast<std::size_t>(press_.setting)].volume;
    const float dx = point.x - press_.start.x;
    const float dz = point.z - press_.start.z;
    if (press_.gesture == Gesture::tap) {
      if (std::fabs(dz) > kDragStart * stack.pitch &&
          std::fabs(dz) >= std::fabs(dx)) {
        press_.gesture = Gesture::scroll;
      } else if (volume && std::fabs(dx) > kDragStart * panel->finger) {
        press_.gesture = Gesture::slide;
      }
    }
    if (press_.gesture == Gesture::scroll) {
      panel->first = x2::input::cards_scrolled(stack, press_.start_first, dz);
    } else if (press_.gesture == Gesture::slide) {
      aim_slider(cpu, row, menu, point);
    }
    if (message != kButtonUp) {
      return true;
    }
    press_.held = false;
    if (press_.gesture != Gesture::tap ||
        stack.first + card != press_.setting || card < 0) {
      return true;
    }
    if (volume) {
      aim_slider(cpu, row, menu, point);
      return true;
    }
    const std::uint32_t label = row.label;
    const SceneBox box = text_box(label);
    const ScenePoint aim{0.5F * (box.left + box.right),
                         0.5F * (box.bottom + box.top)};
    forward(cpu, menu, kPointerMove, aim);
    forward(cpu, menu, kButtonDown, aim);
    return true;
  }

  std::optional<x2::native::OptionsTouchCard> card(std::uint32_t label) const {
    for (const auto &[menu, panel] : panels_) {
      if (!panel.drawn) {
        continue;
      }
      for (std::size_t i = 0; i < panel.rows.size(); ++i) {
        const Row &row = panel.rows[i];
        if (row.label != label || !row.shown) {
          continue;
        }
        x2::native::OptionsTouchCard out{row.card, std::nullopt};
        if (kSettings[i].volume) {
          out.bar = row.bar;
        }
        return out;
      }
    }
    return std::nullopt;
  }

private:
  struct Row {
    std::uint32_t label = 0u;
    std::uint32_t value = 0u;
    std::uint32_t focus = 0u;
    std::uint32_t plain = 0u;
    std::uint32_t lit = 0u;
    /* A volume's slot under its bar. */
    std::uint32_t track = 0u;
    std::uint32_t attached = 0u;
    bool track_attached = false;
    SceneBox card;
    /* A volume card's bar, and the bar's own box retail hit-tests. */
    SceneBox bar;
    SceneBox retail_bar;
    bool shown = false;
    /* A volume's level the finger asks for, and the level and draws since
       its last step. */
    std::optional<float> target;
    float stepped_from = -1.0F;
    int step_draws = 0;
  };

  struct Panel {
    std::array<Row, kSettings.size()> rows{};
    std::uint32_t panel_art = 0u;
    std::uint32_t title = 0u;
    std::uint32_t title_bar = 0u;
    std::uint32_t accept = 0u;
    SceneBox plain_art;
    SceneBox lit_art;
    /* Retail items touch hid, shown again when retail draws. */
    std::vector<std::uint32_t> hidden;
    x2::input::CardStack stack;
    int first = 0;
    float finger = 0.0F;
    bool drawn = false;
    /* The text scale every setting fits at, and the texts it was found
       for. */
    float scale = 1.0F;
    std::string fitted_for;
  };

  enum class Gesture : std::uint8_t { tap, scroll, slide };

  struct Press {
    std::uint32_t menu = 0u;
    bool held = false;
    Gesture gesture = Gesture::tap;
    ScenePoint start;
    int start_first = 0;
    int setting = 0;
  };

  static std::optional<int> setting_of(std::string_view name) {
    for (std::size_t i = 0; i < kSettings.size(); ++i) {
      if (kSettings[i].label == name) {
        return static_cast<int>(i);
      }
    }
    return std::nullopt;
  }

  static std::optional<int> value_of(std::string_view name) {
    for (std::size_t i = 0; i < kSettings.size(); ++i) {
      if (kSettings[i].value == name) {
        return static_cast<int>(i);
      }
    }
    return std::nullopt;
  }

  static std::optional<int> setting_of_bar(const Panel &panel,
                                           std::uint32_t bar) {
    for (std::size_t i = 0; i < panel.rows.size(); ++i) {
      if (kSettings[i].volume && panel.rows[i].value == bar) {
        return static_cast<int>(i);
      }
    }
    return std::nullopt;
  }

  Panel *drawn_panel(std::uint32_t menu) {
    const auto found = panels_.find(menu);
    return found == panels_.end() || !found->second.drawn ? nullptr
                                                          : &found->second;
  }

  Panel &panel_for(x2::guest::GuestCall &call, std::uint32_t menu) {
    const auto found = panels_.find(menu);
    if (found != panels_.end()) {
      return found->second;
    }
    Panel &panel = panels_[menu];
    for (std::size_t i = 0; i < kSettings.size(); ++i) {
      Row &row = panel.rows[i];
      row.label = item_named(call, menu, kSettings[i].label);
      row.value = item_named(call, menu, kSettings[i].value);
      row.focus = item_named(call, menu, kSettings[i].focus);
      row.plain = menu_scene::load_model(call, kCardArt);
      row.lit = menu_scene::load_model(call, kFocusArt);
      if (kSettings[i].volume) {
        row.track = menu_scene::load_model(call, kCardArt);
      }
    }
    panel.panel_art = item_named(call, menu, kPanelArt);
    panel.title = item_named(call, menu, kTitle);
    panel.title_bar = item_named(call, menu, kTitleBar);
    panel.accept = item_named(call, menu, kAccept);
    menu_scene::Vectors vectors;
    if (vectors) {
      panel.plain_art = menu_scene::bounds(call, vectors, panel.rows[0].plain);
      panel.lit_art = menu_scene::bounds(call, vectors, panel.rows[0].lit);
      for (const Row &row : panel.rows) {
        menu_scene::stretch(call, vectors, row.plain, {}, {}, 0.0F);
        menu_scene::stretch(call, vectors, row.lit, {}, {}, 0.0F);
        if (row.track != 0u) {
          menu_scene::stretch(call, vectors, row.track, {}, {}, 0.0F);
        }
      }
    }
    return panel;
  }

  /* The panel art and the rows' focus bars, placed for the retail rows,
     hidden with the menu's own hide (as it hides the controls page). */
  static void hide_retail(x2::guest::GuestCall &call, Panel &panel) {
    const auto hide = [&](std::uint32_t at) {
      if (at == 0u || (RD8(at + item::kFlags) & x2::menu::kItemHidden) != 0u) {
        return;
      }
      call.virtual_call(at, item::kSlotHide, {1u});
      if (std::find(panel.hidden.begin(), panel.hidden.end(), at) ==
          panel.hidden.end()) {
        panel.hidden.push_back(at);
      }
    };
    hide(panel.panel_art);
    for (const Row &row : panel.rows) {
      hide(row.focus);
    }
  }

  void show_retail(x2::guest::GuestCall &call, Panel &panel) {
    if (!panel.drawn && panel.hidden.empty()) {
      return;
    }
    for (const std::uint32_t at : panel.hidden) {
      call.virtual_call(at, item::kSlotHide, {0u});
    }
    panel.hidden.clear();
    for (Row &row : panel.rows) {
      detach(call, row);
      row.shown = false;
      row.target.reset();
    }
    panel.drawn = false;
    press_ = {};
  }

  static void detach(x2::guest::GuestCall &call, Row &row) {
    if (row.attached != 0u) {
      menu_scene::detach(call, row.attached);
      row.attached = 0u;
    }
    if (row.track_attached) {
      menu_scene::detach(call, row.track);
      row.track_attached = false;
    }
  }

  /* A setting's label, and its value or its volume bar, on its card. */
  static void draw_text(x2::guest::GuestCall &call,
                        menu_scene::Vectors &vectors, Panel &panel,
                        const Row &row, std::uint32_t self,
                        const Setting &setting, bool focused) {
    const std::uint32_t box = RD32(self + item::kTextBox);
    const x2::guest::GuestBlock line(menu_text::kLineBytes);
    if (box == 0u || !line) {
      return;
    }
    fit(call, panel, box, line.address());
    const float pad = kTextPad * panel.finger;
    const SceneBox &card = row.card;
    const std::string label = item_text(call, self);
    const bool enabled =
        (RD8(self + item::kFlags) & x2::menu::kItemEnabled) != 0u;
    const float lit = focused ? 1.0F : 0.0F;
    const SceneBox label_area =
        setting.volume ? x2::input::volume_label(card, pad)
                       : SceneBox{card.left + pad, card.bottom,
                                  card.left + pad +
                                      kLabelShare * (card.width() - 2.0F * pad),
                                  card.top};
    put_line(call, self, box, line.address(), label, label_area, panel.scale,
             menu_text::kAlignMiddle,
             menu_scene::style_color(call, vectors, RD32(self + item::kStyle),
                                     lit, enabled, self));
    if (setting.volume || row.value == 0u) {
      return;
    }
    const SceneBox value_area{label_area.right, card.bottom, card.right - pad,
                              card.top};
    put_line(
        call, self, box, line.address(), item_text(call, row.value), value_area,
        panel.scale, menu_text::kAlignRight | menu_text::kAlignMiddle,
        menu_scene::style_color(call, vectors, RD32(row.value + item::kStyle),
                                lit, enabled, row.value));
  }

  static void put_line(x2::guest::GuestCall &call, std::uint32_t self,
                       std::uint32_t box, std::uint32_t line,
                       const std::string &text, const SceneBox &area,
                       float scale, std::uint32_t align, std::uint32_t colour) {
    if (text.empty()) {
      return;
    }
    if (!menu_text::wrap(call, box, line, text, area.width(), scale) &&
        menu_text::is_english(call)) {
      menu_text::cut_with_ellipsis(line);
    }
    menu_text::line(call, self, box, area, scale, align, colour, line);
  }

  /* One text scale at which every label and value fits its part of a
     card. */
  static void fit(x2::guest::GuestCall &call, Panel &panel, std::uint32_t box,
                  std::uint32_t line) {
    const float pad = kTextPad * panel.finger;
    const float width = panel.stack.cards.empty()
                            ? 0.0F
                            : panel.stack.cards[0].width() - 2.0F * pad;
    std::vector<std::pair<std::string, float>> texts;
    std::string key;
    for (std::size_t i = 0; i < panel.rows.size(); ++i) {
      const Row &row = panel.rows[i];
      if (row.label == 0u) {
        continue;
      }
      const bool volume = kSettings[i].volume;
      texts.emplace_back(item_text(call, row.label),
                         volume ? width : kLabelShare * width);
      if (!volume && row.value != 0u) {
        texts.emplace_back(item_text(call, row.value),
                           (1.0F - kLabelShare) * width);
      }
    }
    for (const auto &[text, room] : texts) {
      key += text;
      key.push_back('\n');
    }
    key += std::to_string(panel.finger);
    if (key == panel.fitted_for) {
      return;
    }
    const float high =
        x2::input::kCardText * panel.finger / menu_text::line_height(call, box);
    panel.scale = menu_text::fitted_scale(high, [&](float scale) {
      return std::all_of(texts.begin(), texts.end(), [&](const auto &text) {
        return text.first.empty() ||
               menu_text::wrap(call, box, line, text.first, text.second, scale);
      });
    });
    panel.fitted_for = key;
  }

  /* The retail bar point a finger at `point` on a volume card asks for. */
  static ScenePoint bar_point(const Row &row, float share) {
    return {row.retail_bar.left + share * row.retail_bar.width(),
            0.5F * (row.retail_bar.bottom + row.retail_bar.top)};
  }

  void aim_slider(const CPU &cpu, Row &row, std::uint32_t menu,
                  ScenePoint point) {
    const float share = x2::input::bar_share(row.bar, point.x);
    if (!row.target) {
      /* Retail focuses a bar's row on the first message over it. */
      forward(cpu, menu, kPointerMove, bar_point(row, share));
      row.stepped_from = -1.0F;
    }
    row.target = share;
  }

  /* One retail click on the bar toward the level the finger asks for, once
     the last one has moved it. */
  void step_toward_finger(const CPU &cpu, Row &row, std::uint32_t bar) {
    if (!row.target) {
      return;
    }
    const float fill = RDF32(bar + kBarFill);
    const bool held = press_.held && press_.gesture == Gesture::slide;
    if (std::fabs(*row.target - fill) <= kHalfStep) {
      if (!held) {
        row.target.reset();
      }
      return;
    }
    if (fill == row.stepped_from && ++row.step_draws < kStepDraws) {
      return;
    }
    if (fill == row.stepped_from && !held) {
      row.target.reset();
      return;
    }
    row.stepped_from = fill;
    row.step_draws = 0;
    const std::uint32_t menu = RD32(bar + item::kMenu);
    const ScenePoint at = bar_point(row, *row.target);
    forward(cpu, menu, kPointerMove, at);
    forward(cpu, menu, kButtonDown, at);
  }

  /* A message to retail's handler, as the pointer would send it. */
  void forward(const CPU &cpu, std::uint32_t menu, std::uint32_t message,
               ScenePoint at) {
    forwarding_ = true;
    x2::guest::GuestCall(cpu).thiscall(
        exe_linked(kOnMouse), menu,
        {word(at.x), word(at.z), kButtonHeld, message, 0u});
    forwarding_ = false;
  }

  std::unordered_map<std::uint32_t, Panel> panels_;
  Press press_;
  bool forwarding_ = false;
};

OptionsTouch g_options_touch;

} // namespace

bool x2::native::options_touch_draw(const CPU &cpu, std::uint32_t self,
                                    std::string_view menu_class,
                                    std::string_view name) {
  return menu_class == "CMenuOptions" && g_options_touch.draw(cpu, self, name);
}

void x2::native::options_touch_restore(const CPU &cpu, std::uint32_t self) {
  g_options_touch.restore(cpu, self);
}

void x2::native::options_touch_destroy(const CPU &cpu, std::uint32_t self) {
  g_options_touch.destroy(cpu, self);
}

std::optional<x2::native::OptionsTouchCard>
x2::native::options_touch_card(std::uint32_t label) {
  return g_options_touch.card(label);
}

extern "C" void x2_override_005bdac0(CPU *cpu) {
  g_options_touch.draw_bar(cpu, cpu->reg[kX86pEcx]);
}

extern "C" void x2_override_005cb5f0(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const ScenePoint point{
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 4u))),
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 8u)))};
  if (g_options_touch.pointer(*cpu, cpu->reg[kX86pEcx], point,
                              RD32(esp + 16u))) {
    cpu->reg[kX86pEax] &= ~0xffu;
    cpu->reg[kX86pEsp] += 4u + 20u;
    return;
  }
  x86_guest_body(cpu, "XMen2.exe", kOnMouse);
}

__attribute__((constructor)) static void x2_options_touch_register(void) {
  x86_register_override("XMen2.exe", kBarDraw, x2_override_005bdac0);
  x86_register_override("XMen2.exe", kOnMouse, x2_override_005cb5f0);
}
