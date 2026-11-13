/*
 * The port's rows of the retail Options screen (plan #203), in a column of
 * the game's option cards right of the retail panel, between the top of its
 * title bar and the bottom of its Accept bar. The focused row is lit; the
 * column scrolls to keep it shown. A pointer on a card focuses its row and a
 * click or tap steps it; a drag or the wheel scrolls the column.
 *
 * A port row has no node of its own, so its text box is never rendered and
 * never cleared, and a line put there holds glyphs of the game's shared pool
 * until it runs out for every menu (external/xmen2-decomp/docs/notes/menus.md). The rows' text goes
 * through the Accept label's box instead, which the game renders and clears
 * each frame, drawn when that label draws.
 */
#include "options_port_column.hpp"

#include "../input/menu_touch_gaps.hpp"
#include "../input/touch_cards.hpp"
#include "../input/touch_runtime.h"
#include "guest_call.hpp"
#include "guest_modules.h"
#include "menu_item_access.hpp"
#include "menu_item_memory.hpp"
#include "menu_scene.hpp"
#include "menu_text.hpp"
#include "option_card.hpp"
#include "port_option_rows.hpp"
#include "retail_menu_model.hpp"
#include "touch_menu_source.hpp"
#include "x86rt.h"

#include <algorithm>
#include <cmath>
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
using x2::presentation::ScenePoint;

constexpr std::uint32_t kPage = x2::menu::kMenuPage;
constexpr std::uint32_t kButtonDown = 0x201u;
constexpr std::uint32_t kButtonUp = 0x202u;
constexpr std::uint32_t kPointerMove = 0x200u;
constexpr std::uint32_t kWheel = 0x20au;

/* The title bar's box gives the panel's width; heights come from the text
   labels, as a model item's box is not in the scene's z. */
constexpr std::string_view kTitleBar = "title";
constexpr std::string_view kTitleLabel = "label_options";
/* The text item whose box and depth the rows' text and cards use. */
constexpr std::string_view kTextHost = "label_accept";
/* One of Options' own rows, absent from the port's pages. */
constexpr std::string_view kRetailRow = "label_vibration";
constexpr float kCardDepth = 5.0F;
/* The gap between the retail panel and the column, and the column's margin
   from the plane's right edge, as shares of the panel's width. */
constexpr float kGap = 0.06F;
/* Text inset from a card's ends, and the label's share of the text width,
   in card heights and of the width. */
constexpr float kTextPad = 0.3F;
constexpr float kLabelShare = 0.55F;
/* A drag scrolls after this share of a card pitch. */
constexpr float kDragStart = 0.25F;
constexpr float kBarLabels = 1.5F;
/* Shortest desktop card in row text heights; past it the column scrolls. */
constexpr float kMinCardLabels = 1.0F;

int s16(std::uint32_t address) {
  return static_cast<std::int16_t>(RD16(address));
}

class PortColumn {
public:
  /* True for a port row, which draws nothing itself; the Accept label's
     draw, which the game renders, draws the whole column and then its own
     text. */
  bool draw(const CPU &cpu, std::uint32_t self) {
    const std::uint32_t menu = RD32(self + item::kMenu);
    x2::guest::GuestCall call(cpu);
    Column &column = column_for(call, menu);
    if (row_index(column, self)) {
      return true;
    }
    if (self != column.text_host || column.text_host == 0u) {
      return false;
    }
    const auto plane = x2::native::live_scene_plane();
    menu_scene::Vectors vectors;
    if (!plane || !vectors || column.title_bar == 0u ||
        column.title_label == 0u) {
      return false;
    }
    const bool page = RD32(menu + kPage) == 0u;
    const std::uint32_t focus = RD32(menu + x2::menu::kMenuFocus);
    if (page) {
      layout(column, *plane, focus);
    }
    const float depth = static_cast<float>(s16(self + item::kDepth));
    for (std::size_t index = 0; index < column.rows.size(); ++index) {
      Row &row = column.rows[index];
      detach(call, row);
      const int shown = static_cast<int>(index) - column.stack.first;
      row.shown = page && shown >= 0 &&
                  shown < static_cast<int>(column.stack.cards.size());
      if (!row.shown) {
        item::set_box(row.item, {});
        continue;
      }
      row.card = column.stack.cards[static_cast<std::size_t>(shown)];
      item::set_box(row.item, row.card);
      const bool focused = focus == row.item;
      row.art.show(call, vectors, row.card, depth + kCardDepth, focused);
      menu_text::label_and_value(call, self, row.item, row.card,
                                 x2::native::port_option_label(*row.option),
                                 x2::native::port_option_value(*row.option),
                                 focused, kTextPad, kLabelShare);
    }
    return false;
  }

  void destroy(const CPU &cpu, std::uint32_t self) {
    for (auto at = columns_.begin(); at != columns_.end(); ++at) {
      Column &column = at->second;
      if (column.rows.empty() || column.rows.front().item != self) {
        continue;
      }
      x2::guest::GuestCall call(cpu);
      for (Row &row : column.rows) {
        row.art.release(call);
      }
      columns_.erase(at);
      return;
    }
  }

  bool pointer(const CPU &cpu, std::uint32_t menu, ScenePoint point,
               std::uint32_t message, std::int32_t wheel) {
    const auto found = columns_.find(menu);
    if (found == columns_.end() || RD32(menu + kPage) != 0u) {
      return false;
    }
    Column &column = found->second;
    const int card = x2::input::box_hit(column.stack.cards, point);
    const bool over = column.area.contains(point);
    if (message == kWheel) {
      if (!over) {
        return false;
      }
      column.first = std::max(0, column.stack.first + (wheel > 0 ? -1 : 1));
      column.follow_focus = false;
      return true;
    }
    if (message == kButtonDown) {
      press_ = {};
      if (card < 0) {
        return false;
      }
      press_ = {menu,
                true,
                false,
                point,
                column.stack.first,
                column.stack.first + card};
      return true;
    }
    if (press_.held && press_.menu == menu) {
      const float dz = point.z - press_.start.z;
      if (!press_.dragged && std::fabs(dz) > kDragStart * column.stack.pitch) {
        press_.dragged = true;
      }
      if (press_.dragged) {
        column.first =
            x2::input::cards_scrolled(column.stack, press_.start_first, dz);
        column.follow_focus = false;
      }
      if (message == kButtonUp) {
        press_.held = false;
        if (!press_.dragged && column.stack.first + card == press_.row &&
            card >= 0) {
          choose(cpu, menu, column.rows[static_cast<std::size_t>(press_.row)]);
        }
      }
      return true;
    }
    if (message == kPointerMove && card >= 0 &&
        !x2::input::touch_runtime_active()) {
      /* A mouse hovering a card focuses its row, as on the retail rows. */
      focus(cpu, menu,
            column.rows[static_cast<std::size_t>(column.stack.first + card)]);
      return true;
    }
    return card >= 0;
  }

private:
  struct Row {
    std::uint32_t item = 0u;
    x2::native::OptionCard art;
    SceneBox card;
    bool shown = false;
    const x2::native::PortOptionRow *option = nullptr;
  };

  struct Column {
    std::vector<Row> rows;
    std::uint32_t title_bar = 0u;
    std::uint32_t title_label = 0u;
    std::uint32_t text_host = 0u;
    /* Options' own rows are left of the column; a port page has none. */
    bool beside_panel = true;
    SceneBox area;
    x2::input::CardStack stack;
    int first = 0;
    /* Scroll to the focused row until a drag or the wheel moves it. */
    bool follow_focus = true;
    std::uint32_t last_focus = 0u;
  };

  struct Press {
    std::uint32_t menu = 0u;
    bool held = false;
    bool dragged = false;
    ScenePoint start;
    int start_first = 0;
    int row = 0;
  };

  static std::optional<std::size_t> row_index(const Column &column,
                                              std::uint32_t self) {
    for (std::size_t i = 0; i < column.rows.size(); ++i) {
      if (column.rows[i].item == self) {
        return i;
      }
    }
    return std::nullopt;
  }

  Column &column_for(x2::guest::GuestCall &call, std::uint32_t menu) {
    const auto found = columns_.find(menu);
    if (found != columns_.end()) {
      return found->second;
    }
    Column &column = columns_[menu];
    for (const auto *option : x2::native::port_option_rows()) {
      Row row;
      row.option = option;
      row.item = item::named(call, menu, x2::native::port_option_item(*option));
      if (row.item == 0u) {
        continue;
      }
      column.rows.push_back(row);
    }
    column.title_bar = item::named(call, menu, kTitleBar);
    column.beside_panel = item::named(call, menu, kRetailRow) != 0u;
    column.title_label = item::named(call, menu, kTitleLabel);
    column.text_host = item::named(call, menu, kTextHost);
    menu_scene::Vectors vectors;
    if (vectors && !column.rows.empty()) {
      for (Row &row : column.rows) {
        row.art.load(call, vectors, false);
      }
    }
    return column;
  }

  /* The column right of the panel, or from the title's left edge on a page
     without one; finger-tall cards on touch, on desktop as tall as lets
     every row show, at most a finger and at least a readable row. */
  static void layout(Column &column,
                     const x2::presentation::RetailScenePlane &plane,
                     std::uint32_t focus) {
    const SceneBox title = item::box(column.title_bar);
    const SceneBox label = item::box(column.title_label);
    const SceneBox accept = item::box(column.text_host);
    /* On a page the column runs between the title and Accept bars, each a
       label and this many label heights of art around it. */
    const float bar = kBarLabels * label.height();
    const float top = column.beside_panel ? label.top : label.bottom - bar;
    const float bottom = column.beside_panel ? accept.bottom : accept.top + bar;
    const float gap = kGap * title.width();
    const float plane_right = plane.left() + plane.width();
    const float margin = std::max(0.0F, title.left - plane.left());
    const float left = column.beside_panel ? title.right + gap : title.left;
    column.area = {left, bottom, std::max(left, plane_right - margin), top};
    const int rows = static_cast<int>(column.rows.size());
    const float finger = x2::input::menu_finger_units(plane);
    const float fitting =
        column.area.height() / (static_cast<float>(rows) *
                                (x2::input::kCardHeight + x2::input::kCardGap));
    const float readable = std::min(finger, kMinCardLabels * accept.height());
    const float card = x2::input::touch_runtime_active()
                           ? finger
                           : std::clamp(fitting, readable, finger);
    if (focus != column.last_focus) {
      column.last_focus = focus;
      column.follow_focus = true;
    }
    column.stack =
        x2::input::stack_cards(column.area, card, rows, column.first, rows);
    if (const auto at = row_index(column, focus); at && column.follow_focus) {
      const int index = static_cast<int>(*at);
      if (index < column.stack.first) {
        column.first = index;
      } else if (index >= column.stack.first + column.stack.rows) {
        column.first = index - column.stack.rows + 1;
      }
      column.stack =
          x2::input::stack_cards(column.area, card, rows, column.first, rows);
    }
    column.first = column.stack.first;
  }

  static void detach(x2::guest::GuestCall &call, Row &row) {
    row.art.hide(call);
  }

  static void focus(const CPU &cpu, std::uint32_t menu, const Row &row) {
    if (RD32(menu + x2::menu::kMenuFocus) != row.item) {
      x2::guest::GuestCall call(cpu);
      item::focus(call, menu, row.item);
    }
  }

  static void choose(const CPU &cpu, std::uint32_t menu, const Row &row) {
    focus(cpu, menu, row);
    x2::native::port_option_choose(cpu, *row.option, 1);
  }

  std::unordered_map<std::uint32_t, Column> columns_;
  Press press_;
};

PortColumn g_column;

} // namespace

bool x2::native::options_port_column_draw(const CPU &cpu, std::uint32_t self) {
  const std::uint32_t image = x2::native::guest_exe_base();
  return std::string_view(x2::menu::classify_menu(
             RD32(RD32(self + item::kMenu)), image)) == "CMenuOptions" &&
         g_column.draw(cpu, self);
}

bool x2::native::options_port_column_has_card(std::string_view menu_class,
                                              std::string_view name) {
  return menu_class == "CMenuOptions" &&
         x2::native::port_option_row_of_item(name) != nullptr;
}

void x2::native::options_port_column_destroy(const CPU &cpu,
                                             std::uint32_t self) {
  g_column.destroy(cpu, self);
}

bool x2::native::options_port_column_pointer(const CPU &cpu, std::uint32_t menu,
                                             ScenePoint point,
                                             std::uint32_t message,
                                             std::int32_t wheel) {
  return g_column.pointer(cpu, menu, point, message, wheel);
}
