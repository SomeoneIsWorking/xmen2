#include "control_menu_route.hpp"

#include "../input/menu_touch_gaps.hpp"
#include "../input/touch_menu_parts.hpp"
#include "../input/touch_runtime_menu.hpp"
#include "../input/touch_targets.hpp"
#include "control.h"
#include "control_command_bridge.h"
#include "control_query.h"
#include "json_string.h"
#include "list_touch.hpp"
#include "options_touch.hpp"
#include "popup_touch_drawn.hpp"
#include "touch_menu_source.hpp"
#include "win32_pointer.h"
#include "x86rt_native.h"

#include <cstdio>
#include <vector>

namespace x2::control {
namespace {

void put_string(std::string *out, const std::string &value) {
  std::vector<char> buffer(value.size() * 6u + 3u);
  x2::native::json_string_format(buffer.data(), buffer.size(), value.c_str());
  out->append(buffer.data());
}

void put_key(std::string *out, const char *key) {
  out->push_back('"');
  out->append(key);
  out->append("\":");
}

void put_bool(std::string *out, const char *key, bool value) {
  put_key(out, key);
  out->append(value ? "true," : "false,");
}

void put_int(std::string *out, const char *key, long value) {
  put_key(out, key);
  out->append(std::to_string(value));
  out->push_back(',');
}

void put_text(std::string *out, const char *key, const std::string &value) {
  put_key(out, key);
  put_string(out, value);
  out->push_back(',');
}

void put_rect(std::string *out, const menu::SceneRect &rect) {
  char text[96];
  std::snprintf(text, sizeof text,
                "\"rect\":{\"left\":%d,\"top\":%d,\"right\":%d,\"bottom\":%d},",
                rect.left, rect.top, rect.right, rect.bottom);
  out->append(text);
}

/* Where /touch lands on the box the game hit-tests, as window fractions. */
void put_tap(std::string *out, const menu::SceneRect &rect,
             const presentation::RetailScenePlane *plane) {
  float x = 0.0F;
  float y = 0.0F;
  if (plane == nullptr) {
    return;
  }
  const presentation::ClientPoint client =
      input::menu_item_centre(rect, *plane);
  if (native::win32_pointer_client_to_window(client.x, client.y, &x, &y) == 0) {
    return;
  }
  char text[64];
  std::snprintf(text, sizeof text, "\"tap\":{\"x\":%.5f,\"y\":%.5f},", x, y);
  out->append(text);
}

/* A box's height in fingers. */
void put_fingers(std::string *out, const input::SceneBox &box,
                 const presentation::RetailScenePlane *plane) {
  if (plane == nullptr) {
    return;
  }
  char text[40];
  std::snprintf(
      text, sizeof text, "\"fingers\":%.3f,",
      static_cast<double>(box.height() / input::menu_finger_units(*plane)));
  out->append(text);
}

void put_fill(std::string *out, const std::optional<float> &fill) {
  put_key(out, "fill");
  if (fill) {
    char text[32];
    std::snprintf(text, sizeof text, "%.3f,", static_cast<double>(*fill));
    out->append(text);
  } else {
    out->append("null,");
  }
}

void close_object(std::string *out) {
  if (!out->empty() && out->back() == ',') {
    out->pop_back();
  }
  out->push_back('}');
}

void put_row(std::string *out, const menu::MenuSnapshot &menu,
             const menu::MenuItem &item,
             const presentation::RetailScenePlane *plane) {
  out->push_back('{');
  put_int(out, "slot", static_cast<long>(item.slot));
  put_text(out, "name", item.name);
  put_text(out, "class", menu::item_class_name(item.item_class));
  put_text(out, "label", item.label);
  const menu::MenuItem *value =
      item.value_item >= 0
          ? &menu.items[static_cast<std::size_t>(item.value_item)]
          : nullptr;
  put_key(out, "value");
  if (value) {
    put_string(out, value->label);
    out->push_back(',');
  } else {
    out->append("null,");
  }
  put_text(out, "value_item", value ? value->name : std::string());
  put_fill(out, value ? value->fill : item.fill);
  put_rect(out, item.rect);
  put_tap(out, item.rect, plane);
  put_bool(out, "enabled", item.enabled());
  put_bool(out, "hidden", item.hidden());
  put_bool(out, "never_focus", item.never_focus());
  put_bool(out, "focused", item.focused);
  put_bool(out, "has_use", !item.use_command.empty());
  put_bool(out, "has_left_right", item.has_left_right());
  put_text(out, "use", item.use_command);
  put_text(out, "left", item.left_command);
  put_text(out, "right", item.right_command);
  close_object(out);
}

void put_text_item(std::string *out, const menu::MenuItem &item,
                   const presentation::RetailScenePlane *plane) {
  out->push_back('{');
  put_int(out, "slot", static_cast<long>(item.slot));
  put_text(out, "name", item.name);
  put_text(out, "class", menu::item_class_name(item.item_class));
  put_text(out, "label", item.label);
  put_rect(out, item.rect);
  put_tap(out, item.rect, plane);
  put_bool(out, "enabled", item.enabled());
  close_object(out);
}

void put_strings(std::string *out, const char *key,
                 const std::vector<std::string> &strings) {
  put_key(out, key);
  out->push_back('[');
  for (const std::string &text : strings) {
    put_string(out, text);
    out->push_back(',');
  }
  if (out->back() == ',') {
    out->pop_back();
  }
  out->push_back(']');
}

void put_list_box(std::string *out, const menu::ListBoxState &list) {
  put_key(out, "list_box");
  out->push_back('{');
  put_int(out, "top", list.top);
  put_int(out, "selected", list.selected);
  put_int(out, "visible_rows", list.visible_rows);
  put_int(out, "row_height", list.row_height);
  put_strings(out, "entries", list.entries);
  out->push_back(',');
  std::vector<std::string> values;
  for (std::size_t i = 0; i < list.entries.size(); ++i) {
    values.push_back(list.value(i));
  }
  put_strings(out, "values", values);
  out->append("},");
}

void put_hero(std::string *out, const native::HeroRecord &hero) {
  out->push_back('{');
  put_text(out, "name", hero.name);
  put_text(out, "display_name", hero.display_name);
  put_int(out, "level", hero.level);
  put_bool(out, "fallen", hero.fallen);
  put_bool(out, "unlocked", hero.unlocked);
  put_strings(out, "power_slots",
              {hero.power_slots.begin(), hero.power_slots.end()});
  out->push_back(',');
  close_object(out);
}

/* The point `across` and `up` of the way over a touch box, as a tap named
   `key`. */
void put_box_point(std::string *out, const char *key,
                   const input::SceneBox &box, float across, float up,
                   const presentation::RetailScenePlane *plane) {
  float x = 0.0F;
  float y = 0.0F;
  if (plane == nullptr) {
    return;
  }
  const presentation::ClientPoint client = plane->to_client(
      {box.left + across * box.width(), box.bottom + up * box.height()});
  if (native::win32_pointer_client_to_window(client.x, client.y, &x, &y) == 0) {
    return;
  }
  char text[96];
  std::snprintf(text, sizeof text, "\"%s\":{\"x\":%.5f,\"y\":%.5f},", key, x,
                y);
  out->append(text);
}

/* A touch box's centre as a tap. */
void put_box_tap(std::string *out, const input::SceneBox &box,
                 const presentation::RetailScenePlane *plane) {
  put_box_point(out, "tap", box, 0.5F, 0.5F, plane);
}

/* A list box's cards where its last touch draw put them, each with the
   entry it shows. */
void put_list_touch(std::string *out, std::uint32_t list,
                    const presentation::RetailScenePlane *plane) {
  const auto drawn = native::list_touch_drawn(list);
  if (!drawn) {
    return;
  }
  put_key(out, "touch_cards");
  out->push_back('[');
  for (std::size_t i = 0; i < drawn->cards.size(); ++i) {
    out->push_back('{');
    put_box_tap(out, drawn->cards[i], plane);
    put_int(out, "entry",
            static_cast<long>(drawn->first) + static_cast<long>(i));
    close_object(out);
    out->push_back(',');
  }
  if (out->back() == ',') {
    out->pop_back();
  }
  out->append("],");
}

/* A CMenuOptions setting's card where its last touch draw put it: its
   centre and a point near its top corner, its height over a finger's, and a
   volume card's bar near each end, as taps. */
void put_options_touch(std::string *out, std::uint32_t label,
                       const presentation::RetailScenePlane *plane) {
  const auto card = native::options_touch_card(label);
  if (!card) {
    return;
  }
  put_key(out, "touch_card");
  out->push_back('{');
  put_box_tap(out, card->card, plane);
  put_box_point(out, "edge", card->card, 0.1F, 0.9F, plane);
  put_fingers(out, card->card, plane);
  if (card->bar) {
    put_box_point(out, "bar_low", *card->bar, 0.1F, 0.5F, plane);
    put_box_point(out, "bar_high", *card->bar, 0.9F, 0.5F, plane);
  }
  close_object(out);
  out->push_back(',');
}

void put_item(std::string *out, const menu::MenuItem &item,
              const presentation::RetailScenePlane *plane) {
  out->push_back('{');
  put_int(out, "slot", static_cast<long>(item.slot));
  put_text(out, "name", item.name);
  put_text(out, "class", menu::item_class_name(item.item_class));
  put_text(out, "label", item.label);
  put_rect(out, item.rect);
  put_tap(out, item.rect, plane);
  put_int(out, "flags", item.flags);
  put_bool(out, "focused", item.focused);
  put_bool(out, "navigable", item.navigable);
  put_fill(out, item.fill);
  put_text(out, "use", item.use_command);
  put_text(out, "left", item.left_command);
  put_text(out, "right", item.right_command);
  put_int(out, "link_up", item.link_up);
  put_int(out, "link_down", item.link_down);
  put_int(out, "link_left", item.link_left);
  put_int(out, "link_right", item.link_right);
  if (item.list_box) {
    put_list_box(out, *item.list_box);
    put_list_touch(out, item.address, plane);
  }
  put_options_touch(out, item.address, plane);
  if (item.item_class == menu::ItemClass::char_summary) {
    put_bool(out, "masks_locked", item.masks_locked);
  }
  if (item.hero) {
    put_key(out, "hero");
    put_hero(out, *item.hero);
    out->push_back(',');
  }
  if (!item.heroes.empty()) {
    put_key(out, "heroes");
    out->push_back('[');
    for (const std::optional<native::HeroRecord> &hero : item.heroes) {
      if (hero) {
        put_hero(out, *hero);
      } else {
        out->append("null");
      }
      out->push_back(',');
    }
    out->back() = ']';
    out->push_back(',');
  }
  close_object(out);
}

void put_float(std::string *out, const char *key, float value) {
  char text[48];
  std::snprintf(text, sizeof text, "\"%s\":%.1f,", key,
                static_cast<double>(value));
  out->append(text);
}

void put_button(std::string *out, const input::TouchMenuState &state,
                const input::TouchMenuButton &button) {
  out->push_back('{');
  put_text(out, "part", input::touch_menu_part_name(button.part));
  put_int(out, "index", button.index);
  const auto index = static_cast<std::size_t>(button.index);
  if (button.part == input::TouchMenuPart::footer) {
    put_text(out, "label", state.view.footers[index].label);
    put_text(out, "token", state.view.footers[index].token);
  } else if (button.part == input::TouchMenuPart::tab) {
    put_text(out, "label", state.view.tabs[index].label);
  } else {
    put_text(out, "label", state.view.rows[index].label);
  }
  put_float(out, "left", button.rect.left);
  put_float(out, "top", button.rect.top);
  put_float(out, "width", button.rect.right - button.rect.left);
  put_float(out, "height", button.rect.bottom - button.rect.top);
  close_object(out);
}

/* The shown popup page: on touch each card and button where the last draw
   put it; the retail footers otherwise. */
void put_popup(std::string *out, const menu::PopupSnapshot &popup,
               const presentation::RetailScenePlane *plane) {
  const native::PopupTouchDrawn &drawn = native::popup_touch_drawn();
  put_key(out, "popup_page");
  out->push_back('{');
  put_int(out, "selected", popup.selected);
  put_bool(out, "touch", drawn.shown);
  put_key(out, "rows");
  out->push_back('[');
  for (std::size_t i = 0; drawn.shown && i < drawn.cards.size(); ++i) {
    const auto entry = static_cast<std::size_t>(drawn.first_shown) + i;
    if (entry >= popup.entries.size()) {
      break;
    }
    out->push_back('{');
    put_box_tap(out, drawn.cards[i], plane);
    put_int(out, "entry",
            static_cast<long>(drawn.first_entry) + static_cast<long>(i));
    put_text(out, "text", popup.entries[entry]);
    put_bool(out, "enabled", popup.enabled[entry]);
    close_object(out);
    out->push_back(',');
  }
  if (out->back() == ',') {
    out->pop_back();
  }
  out->append("],");
  put_key(out, "footers");
  out->push_back('[');
  if (drawn.shown) {
    for (const native::PopupTouchButton &button : drawn.buttons) {
      out->push_back('{');
      put_box_tap(out, button.box, plane);
      put_text(out, "text", button.label);
      close_object(out);
      out->push_back(',');
    }
  } else {
    for (const menu::PopupFooter &footer : popup.footers) {
      out->push_back('{');
      put_tap(out, footer.rect, plane);
      put_text(out, "text", footer.label);
      close_object(out);
      out->push_back(',');
    }
  }
  if (out->back() == ',') {
    out->pop_back();
  }
  out->append("]},");
}

/* GET /menu's guest read, run at the guest input poll. */
struct MenuRead {
  bool all_items = false;
  menu::ReadStatus status = menu::ReadStatus::ok;
  std::uint32_t failed = 0;
  std::string body;
};

void read_menu_body(void *context) {
  auto *read = static_cast<MenuRead *>(context);
  menu::MenuSnapshot menu;
  read->status = menu::read_live_menu(&menu, &read->failed);
  if (read->status == menu::ReadStatus::ok) {
    const auto plane = native::live_scene_plane();
    read->body = menu_json(menu, read->all_items, plane ? &*plane : nullptr);
  } else if (read->status != menu::ReadStatus::unreadable) {
    read->body = menu.popup_up
                     ? "{\"active\":false,\"popup\":true,\"reason\":"
                     : "{\"active\":false,\"popup\":false,\"reason\":";
    put_string(&read->body, menu::read_status_name(read->status));
    read->body.append("}\n");
  }
}

} // namespace

std::string touch_menu_json(const input::TouchMenuState &state) {
  std::string out = "{";
  put_bool(&out, "visible", state.shown);
  if (!state.shown) {
    close_object(&out);
    return out;
  }
  put_text(&out, "menu", state.view.menu);
  put_text(&out, "title", state.view.title);
  put_int(&out, "focused_row", state.view.focused_row);
  put_float(&out, "viewport_width", state.viewport.width);
  put_float(&out, "viewport_height", state.viewport.height);
  put_float(&out, "scroll", state.layout.scroll);
  put_key(&out, "list");
  out.push_back('{');
  put_float(&out, "top", state.layout.list.top);
  put_float(&out, "bottom", state.layout.list.bottom);
  close_object(&out);
  out.push_back(',');
  put_key(&out, "rows");
  out.push_back('[');
  for (const input::TouchMenuRow &row : state.view.rows) {
    out.push_back('{');
    put_text(&out, "label", row.label);
    put_text(&out, "value", row.value);
    put_fill(&out, row.fill);
    put_bool(&out, "focused", row.focused);
    put_bool(&out, "clicks", row.clicks);
    put_bool(&out, "steps", row.steps);
    close_object(&out);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.append("],");
  put_key(&out, "tabs");
  out.push_back('[');
  for (const input::TouchMenuTab &tab : state.view.tabs) {
    out.push_back('{');
    put_text(&out, "label", tab.label);
    put_bool(&out, "lit", tab.lit);
    close_object(&out);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.append("],");
  put_key(&out, "facts");
  out.push_back('[');
  for (const input::TouchMenuFact &fact : state.view.facts) {
    out.push_back('{');
    put_text(&out, "label", fact.label);
    put_text(&out, "value", fact.value);
    close_object(&out);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.append("],");
  put_strings(&out, "detail", state.view.detail);
  out.push_back(',');
  put_key(&out, "buttons");
  out.push_back('[');
  for (const input::TouchMenuButton &button : state.layout.buttons) {
    put_button(&out, state, button);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.append("]}");
  return out;
}

/* The finger buttons the last frame drew over small items: each one's centre
   and a point near its top corner as taps, its height over a finger's, and
   the scene point a press on it is delivered at. */
void put_touch_targets(std::string *out,
                       const presentation::RetailScenePlane *plane) {
  put_key(out, "touch_buttons");
  out->push_back('[');
  for (const input::TouchTarget &target : input::touch_targets().shown()) {
    out->push_back('{');
    put_box_tap(out, target.area, plane);
    put_box_point(out, "edge", target.area, 0.1F, 0.9F, plane);
    put_fingers(out, target.area, plane);
    put_float(out, "aim_x", target.aim.x);
    put_float(out, "aim_z", target.aim.z);
    close_object(out);
    out->push_back(',');
  }
  if (out->back() == ',') {
    out->pop_back();
  }
  out->append("],");
}

std::string menu_json(const menu::MenuSnapshot &menu, bool all_items,
                      const presentation::RetailScenePlane *plane) {
  std::string out = "{";
  put_bool(&out, "active", true);
  put_text(&out, "menu", menu.name);
  put_text(&out, "class", menu.menu_class);
  char address[16];
  std::snprintf(address, sizeof address, "0x%08x", menu.address);
  put_text(&out, "address", address);
  put_bool(&out, "popup", menu.popup_up);
  if (menu.popup) {
    put_popup(&out, *menu.popup, plane);
  }
  if (menu.mode) {
    put_int(&out, "mode", static_cast<long>(*menu.mode));
  }
  if (menu.assigning_skill) {
    put_int(&out, "assigning_skill", static_cast<long>(*menu.assigning_skill));
  }
  put_int(&out, "item_count", static_cast<long>(menu.items.size()));
  put_touch_targets(&out, plane);
  int focused_row = -1;
  for (std::size_t i = 0; i < menu.rows.size(); ++i) {
    if (menu.rows[i] == menu.focused) {
      focused_row = static_cast<int>(i);
    }
  }
  put_int(&out, "focused_row", focused_row);
  put_key(&out, "desctext");
  out.push_back('[');
  for (std::size_t i = 0; i < menu.desctext.size(); ++i) {
    put_string(&out, menu.desctext[i]);
    out.push_back(i + 1u < menu.desctext.size() ? ',' : ']');
  }
  out.push_back(',');
  put_key(&out, "rows");
  out.push_back('[');
  std::vector<bool> is_row(menu.items.size(), false);
  for (const int row : menu.rows) {
    is_row[static_cast<std::size_t>(row)] = true;
    put_row(&out, menu, menu.items[static_cast<std::size_t>(row)], plane);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.append("],");
  put_key(&out, "texts");
  out.push_back('[');
  for (std::size_t i = 0; i < menu.items.size(); ++i) {
    const menu::MenuItem &item = menu.items[i];
    if (is_row[i] || item.hidden() || item.label.empty()) {
      continue;
    }
    put_text_item(&out, item, plane);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.push_back(']');
  if (all_items) {
    out.append(",\"items\":[");
    for (const menu::MenuItem &item : menu.items) {
      put_item(&out, item, plane);
      out.push_back(',');
    }
    if (out.back() == ',') {
      out.pop_back();
    }
    out.push_back(']');
  }
  out.append("}\n");
  return out;
}

void menu_route(x2::native::Socket fd, const char *query) {
  char items[8] = "";
  MenuRead read;
  read.all_items =
      control_query_arg(query, "items", items, sizeof items) != 0 &&
      std::string(items) == "all";
  if (control_command_guest_read(read_menu_body, &read) < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s, so "
                       "the menu was not read\n");
    return;
  }
  if (read.status == menu::ReadStatus::unreadable) {
    control_reply_text(fd, 500, "Internal Server Error",
                       "the menu model could not be read: guest address "
                       "0x%08x is unreadable\n",
                       read.failed);
    return;
  }
  std::string &body = read.body;
  body.resize(body.size() - 2u);
  body.append(",\"touch_menu\":");
  body.append(touch_menu_json(input::touch_menu_state()));
  body.append("}\n");
  control_reply_json(fd, 200, "OK", body.c_str(), body.size());
}

} // namespace x2::control
