/*
 * CMenuTeam's detail tabs on touch (docs/touch-play.md). The retail panel's
 * stat and ai rows sit 14 scene units apart, so the column left of it, where
 * the hero stands, holds a finger card per row, each drawn by the row's own
 * label item. A tap on a card walks the game's focus there without acting,
 * by the pointer passing over the label as often as onMouse needs to step
 * to it; a stat card's remove and add buttons and a tap on the current ai
 * setting walk there too, then post the pad's X or A. The skills tab's
 * column holds the selected skill with its rank, remove and add buttons and
 * Assign, in place of the game's Assign prompt beside the footers, or while
 * a skill is being assigned the three power slots and Cancel; the
 * hero's name item draws the previous and next hero buttons. Those post the
 * pad's action themselves. The skill Details view the game shows while its
 * prompt is held stays open after the finger lifts, until the next tap on it.
 */
#include "team_touch.hpp"
#include "../input/team_touch_layout.hpp"
#include "../input/touch_runtime.h"
#include "../input/touch_targets.hpp"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_action.hpp"
#include "menu_item_memory.hpp"
#include "menu_scene.hpp"
#include "menu_text.hpp"
#include "retail_menu_model.hpp"
#include "text_item_touch.hpp"
#include "touch_menu_source.hpp"
#include "x86rt_native.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

namespace item = x2::native::menu_item;
namespace list_box = x2::native::list_box;
namespace menu_scene = x2::native::menu_scene;
namespace menu_text = x2::native::menu_text;
using x2::input::SceneBox;
using x2::input::TouchPress;
using x2::native::exe_linked;
using x2::native::MenuAction;

/* CMenuTeam's update (vtable 0x006a2c94 +0x38). */
constexpr std::uint32_t kTeamUpdate = 0x005e66c0u;
/* The menu's screen (0 party, 1 roster, 2 stats, 3 skills, 4 gear, 5 ai,
   6 a skill's Details), the skill being assigned or -1, and the flag
   onMouse sets while the pointer is over the Details prompt. */
constexpr std::uint32_t kMode = 0x18d8u;
constexpr std::uint32_t kAssigning = 0x18e8u;
constexpr std::uint32_t kOverDetails = 0x1d3cu;
constexpr std::uint32_t kStatsMode = 2u;
constexpr std::uint32_t kSkillsMode = 3u;
constexpr std::uint32_t kAiMode = 5u;
constexpr std::uint32_t kSkillDetailsMode = 6u;
/* The menu's player, and the mouse button table FUN_0061a600 reads per
   player (0x28 bytes each, the left button first). */
constexpr std::uint32_t kMenuPlayer = 0x006f3920u;
constexpr std::uint32_t kMouseButtons = 0x00a68ca0u;
constexpr std::uint32_t kMouseButtonsStride = 0x28u;

/* The item's hidden (vt+0x04) and text (vt+0x7c); a list box entry's
   record text. */
constexpr std::uint32_t kSlotHidden = 0x04u;
constexpr std::uint32_t kSlotText = 0x7cu;
constexpr std::uint32_t kEntryText = 0x005bfcd0u;

/* The game's option art: dark for a button, lit for the current row and
   while held. */
constexpr std::string_view kButtonArt = "ui/models/m_pda_option_screen";
constexpr std::string_view kLitArt = "ui/models/m_pda_option_focus";
/* The art's animations play it in; it is shown done. */
constexpr float kSettled = 0.999F;
/* Buttons sit this much behind the text. */
constexpr float kButtonDepth = 5.0F;
/* Text inset from a card's ends, in fingers, and the share of its width the
   art's slanted right end takes. */
constexpr float kTextPad = 0.15F;
constexpr float kArtSlant = 0.12F;
/* A remove or add sign's height, in fingers. */
constexpr float kSignText = 0.7F;
/* A card's name takes its share of the letters, within these bounds. */
constexpr float kNameShareLeast = 0.3F;
constexpr float kNameShareMost = 0.75F;

/* A row the game walks with Up/Down: the label onMouse hit-tests and the
   value beside it. */
struct Row {
  std::string_view label;
  std::string_view value;
};
constexpr std::array<Row, 4> kStats{{
    {"label_body", "body"},
    {"label_focus", "focus"},
    {"label_strike", "strike"},
    {"label_speed", "speed"},
}};
constexpr std::array<Row, 7> kAi{{
    {"label_ai_heal_pickup", "ai_heal_pickup"},
    {"label_ai_heal", "ai_heal"},
    {"label_ai_level", "ai_level"},
    {"label_ai_power", "ai_power"},
    {"label_ai_auto_traits", "ai_auto_traits"},
    {"label_ai_auto_skills", "ai_auto_skills"},
    {"label_ai_auto_equip", "ai_auto_equip"},
}};
/* The skills tab's points label draws its column; the hero's name the hero
   buttons. */
constexpr std::string_view kSkillsOwner = "label_skillpoints";
constexpr std::string_view kSkillList = "skill_list";
/* The skills tab's Assign prompt, shown while the selected skill has a
   rank. */
constexpr std::string_view kAssignPrompt = "assign_help";
constexpr std::string_view kDropToken = "$MENU_DROP";
constexpr std::string_view kHeroOwner = "name";

/* FUN_005f01e0's rank glyphs in a skill's record: one per rank, after 0xdb
   filler; 0xd9 owned, 0xda added in this visit, 0xd8 and 0xd7 not yet. */
constexpr char kRankLocked = '\xd7';
constexpr char kRankOpen = '\xd8';
constexpr char kRankOwned = '\xd9';
constexpr char kRankAdded = '\xda';

/* FUN_005e5900 assigns the skill being assigned to slot 0, 1 or 2 on
   actions 4, 5 and 8, and ends it on 0x15; without one, A adds a rank to
   the selected skill and X takes back one added in this visit. */
struct Command {
  std::string_view label;
  MenuAction action;
};
constexpr std::array<Command, 4> kAssignCommands{{
    {"Power 1", MenuAction::accept},
    {"Power 2", MenuAction::cancel},
    {"Power 3", MenuAction::subtract},
    {"Cancel", MenuAction::back},
}};
/* FUN_005e3c70 turns every tab to the previous or next party hero on
   actions 0x17 and 0x16 (FUN_005da1a0; RT is 0x16). */
constexpr std::array<Command, 2> kHeroCommands{{
    {"Prev hero", MenuAction::prev},
    {"Next hero", MenuAction::next},
}};

int s16(std::uint32_t address) {
  return static_cast<std::int16_t>(RD16(address));
}

std::uint32_t mode_of(std::uint32_t menu) { return RD32(menu + kMode); }

bool assigning(std::uint32_t menu) {
  return static_cast<std::int32_t>(RD32(menu + kAssigning)) >= 0;
}

bool detail_tab(std::uint32_t mode) {
  return mode >= kStatsMode && mode <= kSkillDetailsMode;
}

/* The skill list or a skill's Details, both run by FUN_005e5900. */
bool skills_tab(std::uint32_t mode) {
  return mode == kSkillsMode || mode == kSkillDetailsMode;
}

bool team_menu(std::uint32_t menu) {
  return std::string_view(x2::menu::classify_menu(
             RD32(menu), x2::native::guest_exe_base())) == "CMenuTeam";
}

/* A finger in scene units. */
float finger_units(const x2::presentation::RetailScenePlane &plane) {
  return x2::input::menu_finger_target(plane) * plane.height() /
         static_cast<float>(plane.client_height());
}

/* Letters a player sees: a `$TOKEN` draws as one icon. */
std::size_t shown_letters(std::string_view text) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '$') {
      while (i + 1 < text.size() && text[i + 1] != ' ' && text[i + 1] != '(') {
        ++i;
      }
    }
    ++count;
  }
  return count;
}

/* A selected skill's ranks, "owned/ranks", from its record's glyphs; empty
   for a skill not yet open, whose record names what it needs. */
std::string rank_text(std::string_view glyphs) {
  int owned = 0;
  int ranks = 0;
  for (const char c : glyphs) {
    if (c == kRankOwned || c == kRankAdded) {
      ++owned;
    }
    if (c == kRankOwned || c == kRankAdded || c == kRankOpen ||
        c == kRankLocked) {
      ++ranks;
    }
  }
  return ranks == 0 ? std::string()
                    : std::to_string(owned) + "/" + std::to_string(ranks);
}

/* One button or card of the column. */
struct Part {
  SceneBox box;
  std::string name;
  std::string value;
  bool lit = false;
  /* The name's share of the text's width. */
  float split = 1.0F;
  /* The text's height in fingers, and the column's shared scale for it, or
     0 to fit the part alone. */
  float text = x2::input::kCardText;
  float scale = 0.0F;
  std::optional<x2::input::TouchTarget> target;
  std::optional<MenuAction> action;
  /* The label the game's focus walks to before `action`, or 0. */
  std::uint32_t walk_to = 0u;
};

/* Where a card's text goes: the name from the left, the value at the
   right. */
struct TextSpans {
  SceneBox name;
  SceneBox value;
};

TextSpans text_spans(const Part &part, float finger) {
  const float pad = kTextPad * finger;
  const float left = part.box.left + pad;
  const float inner =
      part.box.width() - pad - std::max(pad, kArtSlant * part.box.width());
  const float split = left + part.split * inner;
  return {{left, part.box.bottom, split - pad, part.box.top},
          {split + pad, part.box.bottom, left + inner, part.box.top}};
}

float name_share(std::string_view name, std::string_view value) {
  if (value.empty()) {
    return 1.0F;
  }
  const auto letters = static_cast<float>(shown_letters(name));
  return std::clamp(
      letters / (letters + static_cast<float>(shown_letters(value) + 1u)),
      kNameShareLeast, kNameShareMost);
}

class TeamTouch {
public:
  void draw(const CPU &cpu, std::uint32_t self, bool touch) {
    const std::uint32_t menu = RD32(self + item::kMenu);
    if (menu == 0u || !team_menu(menu)) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    Owner &owner = owners_[self];
    detach(call, owner);
    if (!touch) {
      return;
    }
    if (owner.name.empty()) {
      owner.name = x2::menu::read_live_item_name(self);
    }
    const auto plane = x2::native::live_scene_plane();
    menu_scene::Vectors vectors;
    const std::uint32_t box = RD32(self + item::kTextBox);
    if (!plane || !vectors || box == 0u ||
        (call.virtual_call(self, kSlotHidden) & 0xffu) != 0u ||
        (RD8(self + item::kFlags) & x2::menu::kItemHidden) != 0u) {
      return;
    }
    const float finger = finger_units(*plane);
    std::vector<Part> parts = parts_for(call, menu, self, owner.name, finger);
    x2::input::TouchTargets &targets = x2::input::touch_targets();
    const float depth = static_cast<float>(s16(self + item::kDepth));
    for (std::size_t i = 0; i < parts.size(); ++i) {
      Part &part = parts[i];
      if (part.target) {
        targets.put(*part.target);
        if (targets.take_tap(part.box)) {
          tapped(menu, part);
        }
      }
      const bool lit = part.lit || targets.pressed(part.box);
      Art &art = art_for(call, vectors, owner, i);
      const std::uint32_t entity = lit ? art.lit : art.plain;
      menu_scene::stretch(call, vectors, entity, lit ? lit_art_ : plain_art_,
                          part.box, depth + kButtonDepth);
      menu_scene::show_phase(call, entity, kSettled);
      menu_scene::attach(call, entity);
      owner.attached.push_back(entity);
      if (part.scale <= 0.0F) {
        part.scale = alone_scale(call, box, part, finger, art);
      }
      words(call, vectors, self, box, part, lit, finger);
    }
  }

  void destroy(const CPU &cpu, std::uint32_t self) {
    const auto found = owners_.find(self);
    if (found == owners_.end()) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    detach(call, found->second);
    for (const Art &art : found->second.art) {
      menu_scene::release(call, art.plain);
      menu_scene::release(call, art.lit);
    }
    owners_.erase(found);
  }

  /* Before CMenuTeam's update: on touch a walk a card started steps on,
     and the Details view a tap opened is held as the mouse holds it, by
     action 9 each update. */
  void before_update(std::uint32_t menu, bool touch) {
    walk(menu, touch);
    const std::uint32_t mode = mode_of(menu);
    if (!touch || menu != details_menu_ ||
        (mode != kSkillsMode && mode != kSkillDetailsMode)) {
      details_open_ = false;
    }
    details_menu_ = menu;
    const std::uint32_t player = RD32(exe_linked(kMenuPlayer));
    const bool held =
        RD8(menu + kOverDetails) != 0u &&
        RD8(exe_linked(kMouseButtons) + player * kMouseButtonsStride) != 0u;
    if (touch && details_held_ && !held) {
      details_open_ = !details_open_;
    }
    details_held_ = held;
    if (details_open_) {
      x2::native::post_menu_action(MenuAction::details);
    }
  }

private:
  /* A tap on a card: walk the game's focus to its label, then act. */
  struct Walk {
    std::uint32_t menu = 0u;
    std::uint32_t mode = 0u;
    std::uint32_t label = 0u;
    x2::presentation::ScenePoint aim;
    std::optional<MenuAction> then;
    std::size_t steps = 0u;
  };

  void tapped(std::uint32_t menu, const Part &part) {
    if (part.walk_to == 0u) {
      if (part.action) {
        x2::native::post_menu_action(*part.action);
      }
      return;
    }
    walk_ = Walk{
        menu, mode_of(menu), part.walk_to, part.target->aim, part.action, 0u};
  }

  /* One step of the walk: onMouse moves the focus one row toward the label
     under the pointer per message, so the pointer passes over it again until
     the label is lit. */
  void walk(std::uint32_t menu, bool touch) {
    if (!walk_) {
      return;
    }
    if (!touch || walk_->menu != menu || mode_of(menu) != walk_->mode ||
        walk_->steps > kAi.size()) {
      walk_.reset();
      return;
    }
    if ((RD8(walk_->label + item::kFlags) & x2::menu::kItemFocusLit) != 0u) {
      if (walk_->then) {
        x2::native::post_menu_action(*walk_->then);
      }
      walk_.reset();
      return;
    }
    x2::input::touch_targets().hover(walk_->aim);
    ++walk_->steps;
  }

  struct Art {
    std::uint32_t plain = 0u;
    std::uint32_t lit = 0u;
    /* A lone part's text scale, and what it was found for. */
    float scale = 1.0F;
    std::string fitted_for;
    float fitted_finger = 0.0F;
  };
  struct Owner {
    std::string name;
    std::vector<Art> art;
    std::vector<std::uint32_t> attached;
  };
  /* The rows of one tab's column fit one text scale: each row's text as
     its own draw last read it. */
  struct Column {
    std::vector<std::string> rows;
    float finger = 0.0F;
    float scale = 1.0F;
    bool fitted = false;
  };

  std::vector<Part> parts_for(x2::guest::GuestCall &call, std::uint32_t menu,
                              std::uint32_t self, std::string_view name,
                              float finger) {
    const std::uint32_t mode = mode_of(menu);
    if (!detail_tab(mode)) {
      return {};
    }
    if (name == kHeroOwner) {
      return assigning(menu) ? std::vector<Part>{}
                             : commands(kHeroCommands, finger, true);
    }
    if (name == kSkillsOwner && skills_tab(mode)) {
      if (assigning(menu)) {
        return commands(kAssignCommands, finger, false);
      }
      return skill_row(call, menu, self, finger);
    }
    if (mode == kStatsMode) {
      return rows(call, menu, self, kStats, name, finger, stats_);
    }
    if (mode == kAiMode) {
      return rows(call, menu, self, kAi, name, finger, ai_);
    }
    return {};
  }

  /* The buttons that post their action, in the hero row or down the
     column. */
  template <std::size_t N>
  static std::vector<Part> commands(const std::array<Command, N> &list,
                                    float finger, bool hero_row) {
    std::vector<SceneBox> boxes;
    if (hero_row) {
      const auto hero = x2::input::team_hero_buttons(finger);
      boxes.assign(hero.begin(), hero.end());
    } else {
      boxes = x2::input::team_rows(static_cast<int>(N), finger);
    }
    std::vector<Part> out;
    for (std::size_t i = 0; i < N && i < boxes.size(); ++i) {
      out.push_back(command(boxes[i], list[i].label, list[i].action));
    }
    return out;
  }

  static Part command(const SceneBox &box, std::string_view label,
                      MenuAction action) {
    Part part;
    part.box = box;
    part.name = std::string(label);
    part.action = action;
    part.target = x2::input::TouchTarget{
        box,
        {0.5F * (box.left + box.right), 0.5F * (box.bottom + box.top)},
        TouchPress::own,
        label};
    return part;
  }

  /* The card `label` owns among `list`: a stat's card and its remove and
     add buttons, or an ai setting's card. */
  template <std::size_t N>
  std::vector<Part> rows(x2::guest::GuestCall &call, std::uint32_t menu,
                         std::uint32_t self, const std::array<Row, N> &list,
                         std::string_view label, float finger, Column &column) {
    const auto at = std::find_if(list.begin(), list.end(), [&](const Row &row) {
      return row.label == label;
    });
    if (at == list.end()) {
      return {};
    }
    const auto index = static_cast<std::size_t>(at - list.begin());
    const SceneBox card =
        x2::input::team_rows(static_cast<int>(N), finger)[index];
    const bool stats = &column == &stats_;
    const bool current =
        (RD8(self + item::kFlags) & x2::menu::kItemFocusLit) != 0u;
    const x2::presentation::ScenePoint aim =
        x2::native::text_item_raw_centre(self);
    Part row;
    row.name = text_of(call, self);
    row.value = text_of(call, item_named(call, menu, at->value));
    row.split = name_share(row.name, row.value);
    row.lit = current;
    row.walk_to = self;
    if (!stats) {
      row.box = card;
      row.target = x2::input::TouchTarget{card, aim, TouchPress::own,
                                          current ? "change" : "select"};
      if (current) {
        row.action = MenuAction::accept;
      }
      row.scale = column_scale(call, RD32(self + item::kTextBox), column, N,
                               index, row, finger);
      return {row};
    }
    const x2::input::TeamStatRow parts = x2::input::team_stat_row(card, finger);
    row.box = parts.name;
    row.target =
        x2::input::TouchTarget{parts.name, aim, TouchPress::own, "select"};
    row.scale = column_scale(call, RD32(self + item::kTextBox), column, N,
                             index, row, finger);
    Part remove;
    remove.box = parts.remove;
    remove.name = "-";
    remove.text = kSignText;
    remove.target =
        x2::input::TouchTarget{parts.remove, aim, TouchPress::own, "remove"};
    remove.action = MenuAction::subtract;
    remove.walk_to = self;
    Part add;
    add.box = parts.add;
    add.name = "+";
    add.text = kSignText;
    add.target = x2::input::TouchTarget{parts.add, aim, TouchPress::own, "add"};
    add.action = MenuAction::accept;
    add.walk_to = self;
    return {row, remove, add};
  }

  /* The selected skill with its ranks, and the buttons that take one back
     and add one. */
  std::vector<Part> skill_row(x2::guest::GuestCall &call, std::uint32_t menu,
                              std::uint32_t self, float finger) {
    const std::uint32_t list = item_named(call, menu, kSkillList);
    if (list == 0u) {
      return {};
    }
    const int selected = s16(list + list_box::kSelected);
    std::string record;
    if (selected >= 0) {
      record = std::string(x2::guest::guest_string(
          call.thiscall(exe_linked(kEntryText), list,
                        {static_cast<std::uint32_t>(selected)}),
          list_box::kRecordTextBytes));
    }
    const std::size_t first_tab = record.find('\t');
    const std::size_t last_tab = record.rfind('\t');
    const std::vector<SceneBox> rows = x2::input::team_rows(3, finger);
    const std::array<SceneBox, 2> buttons =
        x2::input::team_row_halves(rows[1], finger);
    Part row;
    row.box = rows[0];
    row.name = record.substr(0, first_tab);
    if (last_tab != std::string::npos) {
      row.value = rank_text(std::string_view(record).substr(last_tab + 1u));
    }
    row.split = name_share(row.name, row.value);
    row.scale = column_scale(call, RD32(self + item::kTextBox), skill_, 1, 0,
                             row, finger);
    Part remove = command(buttons[0], "-", MenuAction::subtract);
    remove.target->name = "remove";
    remove.text = kSignText;
    Part add = command(buttons[1], "+", MenuAction::accept);
    add.target->name = "add";
    add.text = kSignText;
    std::vector<Part> out{row, remove, add};
    const std::uint32_t prompt = item_named(call, menu, kAssignPrompt);
    if (prompt != 0u &&
        (call.virtual_call(prompt, kSlotHidden) & 0xffu) == 0u &&
        (RD8(prompt + item::kFlags) & x2::menu::kItemHidden) == 0u) {
      const std::string text = text_of(call, prompt);
      if (text.find(kDropToken) != std::string::npos) {
        Part assign = command(rows[2], "assign", MenuAction::drop);
        assign.name = x2::native::menu_token_label(text);
        out.push_back(std::move(assign));
      }
    }
    return out;
  }

  /* The text scale every row of `column` fits at, with row `index` now
     reading as `part`. */
  static float column_scale(x2::guest::GuestCall &call, std::uint32_t box,
                            Column &column, std::size_t rows, std::size_t index,
                            const Part &part, float finger) {
    const std::string key = part.name + '\t' + part.value;
    column.rows.resize(rows);
    if (column.rows[index] != key || column.finger != finger) {
      column.rows[index] = key;
      column.finger = finger;
      column.fitted = false;
    }
    if (column.fitted) {
      return column.scale;
    }
    const x2::guest::GuestBlock line(menu_text::kLineBytes);
    if (!line) {
      return column.scale;
    }
    const float high =
        x2::input::kCardText * finger / menu_text::line_height(call, box);
    column.scale = menu_text::fitted_scale(high, [&](float scale) {
      for (const std::string &row : column.rows) {
        const std::size_t tab = row.find('\t');
        Part shown = part;
        shown.name = row.substr(0, tab);
        shown.value =
            tab == std::string::npos ? std::string() : row.substr(tab + 1u);
        shown.split = name_share(shown.name, shown.value);
        if (!fits(call, box, line.address(), shown, finger, scale)) {
          return false;
        }
      }
      return true;
    });
    column.fitted = true;
    return column.scale;
  }

  static float alone_scale(x2::guest::GuestCall &call, std::uint32_t box,
                           const Part &part, float finger, Art &art) {
    const std::string key = part.name + '\t' + part.value;
    if (key == art.fitted_for && finger == art.fitted_finger) {
      return art.scale;
    }
    const x2::guest::GuestBlock line(menu_text::kLineBytes);
    if (!line) {
      return art.scale;
    }
    const float high = part.text * finger / menu_text::line_height(call, box);
    art.scale = menu_text::fitted_scale(high, [&](float scale) {
      return fits(call, box, line.address(), part, finger, scale);
    });
    art.fitted_for = key;
    art.fitted_finger = finger;
    return art.scale;
  }

  static bool fits(x2::guest::GuestCall &call, std::uint32_t box,
                   std::uint32_t line, const Part &part, float finger,
                   float scale) {
    const TextSpans spans = text_spans(part, finger);
    return menu_text::wrap(call, box, line, part.name, spans.name.width(),
                           scale) &&
           (part.value.empty() || menu_text::wrap(call, box, line, part.value,
                                                  spans.value.width(), scale));
  }

  /* The menu's item named `name` (FUN_005adc10), remembered per menu. */
  std::uint32_t item_named(x2::guest::GuestCall &call, std::uint32_t menu,
                           std::string_view name) {
    if (menu != items_menu_) {
      items_.clear();
      items_menu_ = menu;
    }
    const std::string key(name);
    const auto found = items_.find(key);
    if (found != items_.end()) {
      return found->second;
    }
    const x2::guest::GuestText text(name);
    const std::uint32_t at =
        text
            ? call.thiscall(x2::native::guest_exe_base() + item::kItemByNameRva,
                            menu, {text.address()})
            : 0u;
    items_[key] = at;
    return at;
  }

  static std::string text_of(x2::guest::GuestCall &call, std::uint32_t self) {
    if (self == 0u) {
      return {};
    }
    const std::uint32_t text = call.virtual_call(self, kSlotText);
    return text == 0u ? std::string()
                      : std::string(x2::guest::guest_string(
                            text, menu_text::kLineBytes));
  }

  /* The part's name, centred when it has no value, and its value at the
     right, in the item's font. */
  static void words(x2::guest::GuestCall &call, menu_scene::Vectors &vectors,
                    std::uint32_t self, std::uint32_t box, const Part &part,
                    bool lit, float finger) {
    const x2::guest::GuestBlock line(menu_text::kLineBytes);
    if (!line) {
      return;
    }
    const std::uint32_t colour =
        menu_scene::style_color(call, vectors, RD32(self + item::kStyle),
                                lit ? 1.0F : 0.0F, true, self);
    const TextSpans spans = text_spans(part, finger);
    const std::uint32_t name_align =
        part.value.empty() ? menu_text::kAlignCentre | menu_text::kAlignMiddle
                           : menu_text::kAlignMiddle;
    line_at(call, self, box, line.address(), part.name, spans.name, part.scale,
            name_align, colour);
    if (!part.value.empty()) {
      line_at(call, self, box, line.address(), part.value, spans.value,
              part.scale, menu_text::kAlignRight | menu_text::kAlignMiddle,
              colour);
    }
  }

  static void line_at(x2::guest::GuestCall &call, std::uint32_t self,
                      std::uint32_t box, std::uint32_t line,
                      std::string_view text, const SceneBox &area, float scale,
                      std::uint32_t align, std::uint32_t colour) {
    if (!menu_text::wrap(call, box, line, text, area.width(), scale) &&
        menu_text::is_english(call)) {
      menu_text::cut_with_ellipsis(line);
    }
    menu_text::line(call, self, box, area, scale, align, colour, line);
  }

  Art &art_for(x2::guest::GuestCall &call, menu_scene::Vectors &vectors,
               Owner &owner, std::size_t index) {
    while (owner.art.size() <= index) {
      Art art;
      art.plain = menu_scene::load_model(call, kButtonArt);
      art.lit = menu_scene::load_model(call, kLitArt);
      if (plain_art_.width() <= 0.0F) {
        plain_art_ = menu_scene::bounds(call, vectors, art.plain);
        lit_art_ = menu_scene::bounds(call, vectors, art.lit);
      }
      menu_scene::stretch(call, vectors, art.plain, {}, {}, 0.0F);
      menu_scene::stretch(call, vectors, art.lit, {}, {}, 0.0F);
      owner.art.push_back(std::move(art));
    }
    return owner.art[index];
  }

  static void detach(x2::guest::GuestCall &call, Owner &owner) {
    for (const std::uint32_t entity : owner.attached) {
      menu_scene::detach(call, entity);
    }
    owner.attached.clear();
  }

  std::unordered_map<std::uint32_t, Owner> owners_;
  SceneBox plain_art_;
  SceneBox lit_art_;
  Column stats_;
  Column ai_;
  Column skill_;
  std::uint32_t items_menu_ = 0u;
  std::unordered_map<std::string, std::uint32_t> items_;
  std::uint32_t details_menu_ = 0u;
  bool details_held_ = false;
  bool details_open_ = false;
  std::optional<Walk> walk_;
};

TeamTouch g_team_touch;

} // namespace

void x2::native::team_touch_text_draw(const CPU &cpu, std::uint32_t self,
                                      bool touch) {
  g_team_touch.draw(cpu, self, touch);
}

void x2::native::team_touch_text_destroy(const CPU &cpu, std::uint32_t self) {
  g_team_touch.destroy(cpu, self);
}

bool x2::native::team_touch_hides(std::uint32_t self, std::string_view text) {
  const std::uint32_t menu = RD32(self + item::kMenu);
  if (menu == 0u || !team_menu(menu) || !skills_tab(mode_of(menu))) {
    return false;
  }
  return text.find(kDropToken) != std::string_view::npos ||
         (assigning(menu) && text.find("$MENU_") != std::string_view::npos);
}

extern "C" void x2_override_005e66c0(CPU *cpu) {
  g_team_touch.before_update(cpu->reg[kX86pEcx],
                             x2::input::touch_runtime_active() != 0);
  x86_guest_body(cpu, "XMen2.exe", kTeamUpdate);
}

__attribute__((constructor)) static void x2_team_touch_register(void) {
  x86_register_override("XMen2.exe", kTeamUpdate, x2_override_005e66c0);
}
