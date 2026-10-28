/*
 * CMenuItemListBox on touch (docs/touch-play.md): its entries as finger-tall
 * cards of the game's option art (dark, and lit for the selected entry) with
 * the game's font and icons over them, filling the list's own box; a drag
 * scrolls the cards and a tap selects an entry, a tap on the selected one
 * accepts it. The layout is layout_list_touch; this file draws it in place
 * of the list's draw and takes the list's pointer handler.
 */
#include "list_touch.hpp"
#include "../input/list_touch_layout.hpp"
#include "../input/menu_touch_gaps.hpp"
#include "../input/touch_runtime.h"
#include "guest_body.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_action.hpp"
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
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {

namespace item = x2::native::menu_item;
namespace list = x2::native::list_box;
namespace menu_scene = x2::native::menu_scene;
namespace menu_text = x2::native::menu_text;
using x2::input::SceneBox;
using x2::native::exe_linked;
using x2::presentation::ScenePoint;

constexpr std::uint32_t kListDraw = 0x005c1200u;
/* onMouse(x, z, _, message, _), ret 0x14, al: consumed. */
constexpr std::uint32_t kListOnMouse = 0x005c0e10u;
/* The base destructor every list box class runs. */
constexpr std::uint32_t kListDestroy = 0x005c2800u;

/* The draw's first steps: the item's gamevar refresh (vt+0x24), and the
   scroll arrows (up, down). */
constexpr std::uint32_t kRefresh = 0x005bbd10u;
constexpr std::uint32_t kSetArrows = 0x005bf690u;
constexpr std::uint32_t kEntryCount = 0x005bf750u;
/* An entry's record text, its columns tab-separated. */
constexpr std::uint32_t kEntryText = 0x005bfcd0u;
/* Item slots: hidden, fade, an entry's enabled, own colour (out, entry),
   icons A and B. */
constexpr std::uint32_t kSlotHidden = 0x04u;
constexpr std::uint32_t kSlotFade = 0x0cu;
constexpr std::uint32_t kSlotEnabled = 0x88u;
constexpr std::uint32_t kSlotColor = 0x94u;
constexpr std::uint32_t kSlotIconA = 0xa0u;
constexpr std::uint32_t kSlotIconB = 0xa8u;
/* CMenu+0x324, the focused item. */
constexpr std::uint32_t kMenuFocus = 0x324u;
/* The icon batch's sprite (pos, rgba, size, tile, turn); icon B sits this
   much deeper. */
constexpr std::uint32_t kIcon = 0x005ed120u;
constexpr float kIconBDepth = 10.0F;

constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kPointerMove = 0x200u;

/* The game's option art: dark for an entry, lit for the selected one. */
constexpr std::string_view kCardArt = "ui/models/m_pda_option_screen";
constexpr std::string_view kSelectedArt = "ui/models/m_pda_option_focus";
constexpr int kMaxCards = 10;
/* The art's animations play it in; it is shown done. */
constexpr float kSettled = 0.999F;
/* The dark art's frame along its left edge, a share of its width. */
constexpr float kCardArtFrame = 0.05F;
/* Cards sit this much behind the list's text and icons. */
constexpr float kCardDepth = 5.0F;
/* Draws a tap's accept waits for the selection to arrive. */
constexpr int kAcceptFrames = 60;
/* Retail shows row art past this fade, and arrows once fully in. */
constexpr float kFadeShown = 0.75F;
constexpr float kFadeIn = 1.0F;
/* A drag starts after this share of a card pitch. */
constexpr float kDragStart = 0.25F;

int s16(std::uint32_t address) {
  return static_cast<std::int16_t>(RD16(address));
}

bool on_touch() { return x2::input::touch_runtime_active() != 0; }

bool replaced_class(std::uint32_t self) {
  const std::uint32_t image = x2::native::guest_exe_base();
  return x2::input::list_has_touch_layout(
      x2::menu::classify_item(RD32(self), image),
      x2::menu::classify_menu(RD32(RD32(self + item::kMenu)), image));
}

/* The list's box in the scene, z up. */
SceneBox list_box(std::uint32_t self) {
  const auto left = static_cast<float>(s16(self + item::kBoxLeft));
  const auto top = static_cast<float>(s16(self + item::kBoxTop));
  return {left, top - static_cast<float>(s16(self + item::kBoxHeight)),
          left + static_cast<float>(s16(self + item::kBoxWidth)), top};
}

std::vector<std::string> split_columns(std::string_view text, int columns) {
  std::vector<std::string> out(1);
  for (const char c : text) {
    if (c == '\t' && static_cast<int>(out.size()) < columns) {
      out.emplace_back();
    } else if (c != '\t') {
      out.back().push_back(c);
    }
  }
  return out;
}

class ListTouch {
public:
  /* The touch draw; false leaves the list to retail's. */
  bool draw(const CPU &cpu, std::uint32_t self) {
    if (!replaced_class(self)) {
      return false;
    }
    const auto plane = x2::native::live_scene_plane();
    menu_scene::Vectors vectors;
    if (!plane || !vectors) {
      return false;
    }
    x2::guest::GuestCall call(cpu);
    Cards &cards = cards_for(call, vectors, self);
    call.thiscall(exe_linked(kRefresh), self);
    hide_cursor(call, self);
    detach_all(call, cards);
    cards.touched = true;
    cards.drawn = false;
    const bool hidden = (call.virtual_call(self, kSlotHidden) & 0xffu) != 0u;
    if (hidden || RD32(self + item::kModel) == 0u) {
      call.thiscall(exe_linked(kSetArrows), self, {0u, 0u});
      return true;
    }
    call.virtual_call(self, kSlotFade);
    const auto fade = static_cast<float>(call.x87_result());
    const auto entries =
        static_cast<int>(call.thiscall(exe_linked(kEntryCount), self));
    const std::uint32_t box = RD32(self + item::kTextBox);

    x2::input::ListTouchInput input;
    input.box = list_box(self);
    input.line = menu_text::line_height(call, box);
    input.finger = x2::input::menu_finger_target(*plane) * plane->height() /
                   static_cast<float>(plane->client_height());
    input.entries = entries;
    input.top = s16(self + list::kTop);
    input.icons = (RD8(self + list::kBits) & list::kIcons) != 0u &&
                  RD32(self + list::kIconSheet) != 0u;
    input.art_frame = kCardArtFrame;
    input.max_rows = kMaxCards;
    cards.layout = x2::input::layout_list_touch(input);
    cards.box = input.box;
    const x2::input::CardStack &stack = cards.layout.cards;
    if (stack.rows == 0) {
      return false;
    }
    if (!stack.cards.empty()) {
      const std::string key = entries_key(call, self, entries);
      if (key != cards.fitted_for ||
          cards.layout.text_scale != cards.text_scale_for) {
        cards.text_scale = fitted_scale(call, self, box, entries, cards.layout,
                                        stack.cards[0]);
        cards.fitted_for = key;
        cards.text_scale_for = cards.layout.text_scale;
      }
    }
    WR8(self + list::kVisibleRows, static_cast<std::uint8_t>(stack.rows));
    /* A tap selects a shown card; navigation must not scroll for it. */
    WR8(self + list::kMargin, 0u);
    WR16(self + list::kTop, static_cast<std::uint16_t>(stack.first));

    const std::uint32_t menu = RD32(self + item::kMenu);
    const bool focused = RD32(menu + kMenuFocus) == self;
    const bool arrows = focused && fade >= kFadeIn;
    call.thiscall(exe_linked(kSetArrows), self,
                  {arrows && stack.more_above ? 1u : 0u,
                   arrows && stack.more_below ? 1u : 0u});

    const std::uint8_t flags = RD8(self + item::kFlags);
    const int selected = s16(self + list::kSelected);
    const bool lit = (flags & x2::menu::kItemFocusLit) != 0u;
    const float depth = static_cast<float>(s16(self + item::kDepth));
    const bool english = menu_text::is_english(call);
    for (std::size_t i = 0; i < stack.cards.size(); ++i) {
      const int entry = stack.first + static_cast<int>(i);
      const SceneBox &card = stack.cards[i];
      const bool chosen = lit && entry == selected;
      if (fade > kFadeShown) {
        const std::uint32_t art = chosen ? cards.selected : cards.plain[i];
        const SceneBox &extent = chosen ? cards.selected_art : cards.plain_art;
        menu_scene::stretch(call, vectors, art, extent, card,
                            depth + kCardDepth);
        menu_scene::show_phase(call, art, kSettled);
        menu_scene::attach(call, art);
        cards.attached.push_back(art);
      }
      draw_row(call, vectors, self, box, entry, card, cards.layout,
               cards.text_scale, depth, fade, flags, selected, english);
    }
    accept_arrived(cards, self);
    cards.drawn = true;
    return true;
  }

  /* Retail's own row count and cards gone, when touch has left. */
  void restore(const CPU &cpu, std::uint32_t self) {
    const auto found = lists_.find(self);
    if (found == lists_.end() || !found->second.touched) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    detach_all(call, found->second);
    found->second.touched = false;
    found->second.drawn = false;
    WR8(self + list::kMargin, found->second.margin);
    const int pitch = static_cast<std::int32_t>(RD32(self + list::kRowHeight));
    if (pitch > 0) {
      WR8(self + list::kVisibleRows,
          static_cast<std::uint8_t>(s16(self + item::kBoxHeight) / pitch));
    }
  }

  /* The list's base destructor: its cards go with it. */
  void destroy(const CPU &cpu, std::uint32_t self) {
    const auto found = lists_.find(self);
    if (found == lists_.end()) {
      return;
    }
    x2::guest::GuestCall call(cpu);
    detach_all(call, found->second);
    for (const std::uint32_t card : found->second.plain) {
      menu_scene::release(call, card);
    }
    menu_scene::release(call, found->second.selected);
    lists_.erase(found);
  }

  std::optional<x2::native::ListTouchDrawn> drawn(std::uint32_t self) const {
    const auto found = lists_.find(self);
    if (found == lists_.end() || !found->second.drawn) {
      return std::nullopt;
    }
    const x2::input::CardStack &stack = found->second.layout.cards;
    return x2::native::ListTouchDrawn{stack.first, stack.cards};
  }

  /* The finger on the list's cards: the handler's result, or nullopt to
     leave the message to retail's. */
  std::optional<std::uint32_t> pointer(std::uint32_t self, ScenePoint point,
                                       std::uint32_t message) {
    const auto found = lists_.find(self);
    if (found == lists_.end() || !found->second.drawn ||
        (RD8(self + item::kFlags) & x2::menu::kItemHidden) != 0u) {
      return std::nullopt;
    }
    Cards &cards = found->second;
    x2::input::CardStack &stack = cards.layout.cards;
    if (message == kButtonDown) {
      if (!cards.box.contains(point)) {
        return std::nullopt;
      }
      press_ = {self,  true,        false,
                point, stack.first, x2::input::box_hit(stack.cards, point)};
      return 1u;
    }
    if (!press_.held || press_.list != self) {
      /* Retail walks the selection under a held pointer; a finger
         scrolls. */
      return message == kPointerMove ? std::optional<std::uint32_t>(0u)
                                     : std::nullopt;
    }
    const float dz = point.z - press_.start.z;
    if (!press_.dragging && std::fabs(dz) > kDragStart * stack.pitch) {
      press_.dragging = true;
    }
    if (press_.dragging) {
      stack.first = x2::input::cards_scrolled(stack, press_.start_first, dz);
      WR16(self + list::kTop, static_cast<std::uint16_t>(stack.first));
    }
    if (message != kButtonUp) {
      return 1u;
    }
    press_.held = false;
    const int card = x2::input::box_hit(stack.cards, point);
    if (press_.dragging || card < 0 || card != press_.card) {
      return 1u;
    }
    const int entry = press_.start_first + card;
    if (entry == s16(self + list::kSelected)) {
      owe_accept(cards, entry);
      return 0u;
    }
    walk_to(self, entry);
    /* The codex loads an entry only when it is accepted (0x005b1780). */
    if (x2::menu::classify_item(RD32(self), x2::native::guest_exe_base()) ==
        x2::menu::ItemClass::list_codex) {
      owe_accept(cards, entry);
    }
    return 1u;
  }

private:
  struct Cards {
    std::array<std::uint32_t, kMaxCards> plain{};
    std::uint32_t selected = 0u;
    SceneBox plain_art;
    SceneBox selected_art;
    std::vector<std::uint32_t> attached;
    x2::input::ListTouchLayout layout;
    SceneBox box;
    /* The text scale every entry fits at, and the entries it was found
       for. */
    float text_scale = 1.0F;
    std::string fitted_for;
    float text_scale_for = 0.0F;
    /* Retail's navigation margin, put back when touch leaves. */
    std::uint8_t margin = 0u;
    int accept_on_arrival = -1;
    int accept_frames = 0;
    bool touched = false;
    bool drawn = false;
  };

  struct Press {
    std::uint32_t list = 0u;
    bool held = false;
    bool dragging = false;
    ScenePoint start;
    int start_first = 0;
    int card = -1;
  };

  /* A posted action is a press for one controller round, and the message
     pump runs several before the menu reads them (0x005d3ba0), so a tap's
     accept is posted from the draw. */
  static void owe_accept(Cards &cards, int entry) {
    cards.accept_on_arrival = entry;
    cards.accept_frames = kAcceptFrames;
  }

  /* The accept a tap owes once the selection walk reaches its entry. */
  static void accept_arrived(Cards &cards, std::uint32_t self) {
    if (cards.accept_on_arrival < 0 || RD8(self + list::kWalking) != 0u) {
      return;
    }
    if (s16(self + list::kSelected) == cards.accept_on_arrival) {
      x2::native::post_menu_action(x2::native::MenuAction::accept);
      cards.accept_on_arrival = -1;
    } else if (--cards.accept_frames <= 0) {
      cards.accept_on_arrival = -1;
    }
  }

  /* The walk retail's press on `entry` starts, all of it queued: a step
     published from the pump is lost while the menu does not tick its items
     (the codex as it loads), a queued one waits for the list's tick. */
  static void walk_to(std::uint32_t self, int entry) {
    const int steps = entry - s16(self + list::kSelected);
    if (steps == 0) {
      return;
    }
    WR32(self + list::kWalkSteps, static_cast<std::uint32_t>(steps));
    WR32(self + list::kWalkTimer, 0u);
    WR8(self + list::kWalking, 1u);
  }

  Cards &cards_for(x2::guest::GuestCall &call, menu_scene::Vectors &vectors,
                   std::uint32_t self) {
    const auto found = lists_.find(self);
    if (found != lists_.end()) {
      return found->second;
    }
    Cards &cards = lists_[self];
    cards.margin = RD8(self + list::kMargin);
    for (std::uint32_t &card : cards.plain) {
      card = menu_scene::load_model(call, kCardArt);
    }
    cards.selected = menu_scene::load_model(call, kSelectedArt);
    cards.plain_art = menu_scene::bounds(call, vectors, cards.plain[0]);
    cards.selected_art = menu_scene::bounds(call, vectors, cards.selected);
    for (const std::uint32_t card : cards.plain) {
      menu_scene::stretch(call, vectors, card, {}, {}, 0.0F);
    }
    menu_scene::stretch(call, vectors, cards.selected, {}, {}, 0.0F);
    return cards;
  }

  static void detach_all(x2::guest::GuestCall &call, Cards &cards) {
    for (const std::uint32_t card : cards.attached) {
      menu_scene::detach(call, card);
    }
    cards.attached.clear();
  }

  /* The controller's cursor is not drawn on touch. */
  static void hide_cursor(x2::guest::GuestCall &call, std::uint32_t self) {
    const std::uint8_t state = RD8(self + list::kBits);
    if ((state & list::kCursorAttached) == 0u ||
        RD32(self + list::kCursor) == 0u) {
      return;
    }
    menu_scene::detach(call, RD32(self + list::kCursor));
    WR8(self + list::kBits, state & ~list::kCursorAttached);
  }

  /* Every entry's text, to tell when the fit must be found again. */
  static std::string entries_key(x2::guest::GuestCall &call, std::uint32_t self,
                                 int entries) {
    std::string key;
    for (int e = 0; e < entries; ++e) {
      key += x2::guest::guest_string(
          call.thiscall(exe_linked(kEntryText), self,
                        {static_cast<std::uint32_t>(e)}),
          list::kRecordTextBytes);
      key.push_back('\n');
    }
    return key;
  }

  /* One entry's columns, text and icons over its card. */
  static void draw_row(x2::guest::GuestCall &call, menu_scene::Vectors &vectors,
                       std::uint32_t self, std::uint32_t box, int entry,
                       const SceneBox &card,
                       const x2::input::ListTouchLayout &layout, float scale,
                       float depth, float fade, std::uint8_t flags,
                       int selected, bool english) {
    const auto index = static_cast<std::uint32_t>(entry);
    const bool selected_entry =
        (flags & x2::menu::kItemFocusLit) != 0u && entry == selected;
    const bool enabled =
        (flags & x2::menu::kItemEnabled) != 0u &&
        (call.virtual_call(self, kSlotEnabled, {index}) & 0xffu) != 0u;
    const std::uint32_t own = vectors.put(2, 0.0F, 0.0F, 0.0F);
    call.virtual_call(self, kSlotColor, {own, index});
    const std::uint32_t custom = RDF32(own + 12u) > 0.001F ? own : 0u;
    const std::uint32_t colour =
        menu_scene::style_color(call, vectors, RD32(self + item::kStyle),
                                selected_entry, enabled, self, custom);

    const float mid = 0.5F * (card.bottom + card.top);
    const float pad = layout.pad;
    if (layout.icon > 0.0F) {
      const bool right = (RD8(self + list::kBits) & list::kIconsRight) != 0u;
      const float x = right
                          ? card.right - pad - 0.5F * layout.icon
                          : card.left + layout.icon_inset + 0.5F * layout.icon;
      const std::uint32_t rgba =
          vectors.put(1, 1.0F, 1.0F, 1.0F, std::clamp(fade, 0.0F, 1.0F));
      const std::array<std::uint32_t, 2> tiles = {
          call.virtual_call(self, kSlotIconA, {index}),
          call.virtual_call(self, kSlotIconB, {index})};
      for (std::size_t t = 0; t < tiles.size(); ++t) {
        if (static_cast<std::int32_t>(tiles[t]) < 0) {
          continue;
        }
        const std::uint32_t pos =
            vectors.put(0, x, depth + static_cast<float>(t) * kIconBDepth, mid);
        call.thiscall(exe_linked(kIcon), self + list::kIconBatch,
                      {pos, rgba, x2::guest::float_bits(layout.icon),
                       tiles[t] & 0xffu, 0u});
      }
    }

    const std::vector<std::string> columns = entry_columns(call, self, entry);
    const x2::guest::GuestBlock text(menu_text::kLineBytes);
    if (!text) {
      return;
    }
    const std::vector<Span> spans = column_spans(
        self, columns.size(), card.left + layout.text_inset, card.right - pad);
    for (std::size_t c = 0; c < columns.size(); ++c) {
      const Span &span = spans[c];
      if (columns[c].empty() || !(span.width > 0.0F)) {
        continue;
      }
      if (!menu_text::wrap(call, box, text.address(), columns[c], span.width,
                           scale) &&
          english) {
        menu_text::cut_with_ellipsis(text.address());
      }
      menu_text::line(
          call, self, box,
          {span.from, card.bottom, span.from + span.width, card.top}, scale,
          static_cast<std::uint32_t>(span.align) | menu_text::kAlignMiddle,
          colour, text.address());
    }
  }

  /* An entry's text split at its tabs into the list's columns. */
  static std::vector<std::string> entry_columns(x2::guest::GuestCall &call,
                                                std::uint32_t self, int entry) {
    const int count =
        std::clamp(static_cast<int>(RD8(self + list::kColumnCount)), 1,
                   static_cast<int>(list::kMaxColumns));
    return split_columns(x2::guest::guest_string(
                             call.thiscall(exe_linked(kEntryText), self,
                                           {static_cast<std::uint32_t>(entry)}),
                             list::kRecordTextBytes),
                         count);
  }

  struct Span {
    float from = 0.0F;
    float width = 0.0F;
    int align = 0;
  };

  /* Each column across a card's text from `left` to `right`: retail's column
     widths spread over it, a right-aligned last column over all of it. */
  static std::vector<Span> column_spans(std::uint32_t self, std::size_t columns,
                                        float left, float right) {
    const float spread =
        (right - left) /
        std::max(1.0F, static_cast<float>(s16(self + item::kBoxWidth)));
    std::vector<Span> out;
    float x = left;
    for (std::size_t c = 0; c < columns; ++c) {
      const std::uint32_t column =
          self + list::kColumns +
          static_cast<std::uint32_t>(c) * list::kColumnBytes;
      const int align = s16(column + list::kColumnAlign);
      const float from =
          c + 1u == columns && align == static_cast<int>(menu_text::kAlignRight)
              ? left
              : x;
      out.push_back({from, right - from, align});
      x += static_cast<float>(s16(column)) * spread;
    }
    return out;
  }

  /* The largest text scale up to the layout's at which every entry's columns
     stay on one line; retail's cut is left for what will not fit even at
     the smallest. */
  static float fitted_scale(x2::guest::GuestCall &call, std::uint32_t self,
                            std::uint32_t box, int entries,
                            const x2::input::ListTouchLayout &layout,
                            const SceneBox &card) {
    const x2::guest::GuestBlock text(menu_text::kLineBytes);
    if (!text || entries <= 0) {
      return layout.text_scale;
    }
    std::vector<std::vector<std::string>> rows;
    for (int e = 0; e < entries; ++e) {
      rows.push_back(entry_columns(call, self, e));
    }
    const float pad = layout.pad;
    const auto fits = [&](float scale) {
      for (const std::vector<std::string> &columns : rows) {
        const std::vector<Span> spans =
            column_spans(self, columns.size(), card.left + layout.text_inset,
                         card.right - pad);
        for (std::size_t c = 0; c < columns.size(); ++c) {
          if (!columns[c].empty() &&
              !menu_text::wrap(call, box, text.address(), columns[c],
                               spans[c].width, scale)) {
            return false;
          }
        }
      }
      return true;
    };
    return menu_text::fitted_scale(layout.text_scale, fits);
  }

  std::unordered_map<std::uint32_t, Cards> lists_;
  Press press_;
};

ListTouch g_list_touch;

} // namespace

std::optional<x2::native::ListTouchDrawn>
x2::native::list_touch_drawn(std::uint32_t list) {
  return g_list_touch.drawn(list);
}

extern "C" void x2_override_005c1200(CPU *cpu) {
  const std::uint32_t self = cpu->reg[kX86pEcx];
  if (on_touch() && g_list_touch.draw(*cpu, self)) {
    cpu->reg[kX86pEsp] += 4u;
    return;
  }
  g_list_touch.restore(*cpu, self);
  x86_guest_body(cpu, "XMen2.exe", kListDraw);
}

extern "C" void x2_override_005c0e10(CPU *cpu) {
  const std::uint32_t esp = cpu->reg[kX86pEsp];
  const ScenePoint point{
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 4u))),
      static_cast<float>(static_cast<std::int32_t>(RD32(esp + 8u)))};
  if (on_touch()) {
    if (const auto result =
            g_list_touch.pointer(cpu->reg[kX86pEcx], point, RD32(esp + 16u))) {
      cpu->reg[kX86pEax] = (cpu->reg[kX86pEax] & ~0xffu) | *result;
      cpu->reg[kX86pEsp] += 4u + 20u;
      return;
    }
  }
  x86_guest_body(cpu, "XMen2.exe", kListOnMouse);
}

extern "C" void x2_override_005c2800(CPU *cpu) {
  g_list_touch.destroy(*cpu, cpu->reg[kX86pEcx]);
  x86_guest_body(cpu, "XMen2.exe", kListDestroy);
}

__attribute__((constructor)) static void x2_list_touch_register(void) {
  x86_register_override("XMen2.exe", kListDraw, x2_override_005c1200);
  x86_register_override("XMen2.exe", kListOnMouse, x2_override_005c0e10);
  x86_register_override("XMen2.exe", kListDestroy, x2_override_005c2800);
}
