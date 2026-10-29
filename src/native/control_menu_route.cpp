#include "control_menu_route.hpp"

#include "../input/menu_touch_gaps.hpp"
#include "../input/touch_runtime_menu.hpp"
#include "../input/touch_targets.hpp"
#include "automap_touch.hpp"
#include "control.h"
#include "control_command_bridge.h"
#include "control_query.h"
#include "conversation_panel.hpp"
#include "conversation_touch.hpp"
#include "guest_modules.h"
#include "json_string.h"
#include "list_touch.hpp"
#include "menu_action.hpp"
#include "options_touch.hpp"
#include "popup_touch_drawn.hpp"
#include "touch_menu_source.hpp"
#include "win32_events.h"
#include "win32_pointer.h"
#include "x86rt_native.h"

#include <cctype>
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
      plane->to_client({0.5F * static_cast<float>(rect.left + rect.right),
                        0.5F * static_cast<float>(rect.top + rect.bottom)});
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
    put_fingers(out, drawn->cards[i], plane);
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

void put_scene_box(std::string *out, const char *key,
                   const input::SceneBox &box) {
  put_key(out, key);
  out->push_back('{');
  put_float(out, "left", box.left);
  put_float(out, "bottom", box.bottom);
  put_float(out, "right", box.right);
  put_float(out, "top", box.top);
  close_object(out);
  out->push_back(',');
}

void put_exact(std::string *out, const char *key, float value) {
  char text[64];
  std::snprintf(text, sizeof text, "\"%s\":%.5g,", key,
                static_cast<double>(value));
  out->append(text);
}

/* CMenuAutoMap's view: where the map looks, its turn and bounds in world
   units, and the map's scene units per world unit. */
void put_automap(std::string *out, std::uint32_t menu) {
  const input::MapView view = native::automap_view(menu);
  put_key(out, "automap");
  out->push_back('{');
  put_exact(out, "x", view.at.x);
  put_exact(out, "y", view.at.y);
  put_exact(out, "turn", view.turn);
  put_exact(out, "low_x", view.low.x);
  put_exact(out, "low_y", view.low.y);
  put_exact(out, "high_x", view.high.x);
  put_exact(out, "high_y", view.high.y);
  put_exact(out, "scale", native::automap_scale());
  close_object(out);
  out->push_back(',');
}

/* The shown popup page: on touch each card and button where the last draw
   put it; the retail footers otherwise. */
void put_popup(std::string *out, const menu::PopupSnapshot &popup,
               const presentation::RetailScenePlane *plane) {
  const native::PopupTouchDrawn &drawn = native::popup_touch_drawn();
  put_key(out, "popup_page");
  out->push_back('{');
  put_text(out, "text", popup.title);
  put_int(out, "selected", popup.selected);
  put_bool(out, "touch", drawn.shown);
  if (drawn.shown) {
    put_box_point(out, "frame_low", drawn.frame, 0.0F, 0.0F, plane);
    put_box_point(out, "frame_high", drawn.frame, 1.0F, 1.0F, plane);
    put_box_point(out, "text_low", drawn.text, 0.0F, 0.0F, plane);
    put_box_point(out, "text_high", drawn.text, 1.0F, 1.0F, plane);
  }
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
      put_box_point(out, "edge", button.box, 0.05F, 0.9F, plane);
      put_fingers(out, button.box, plane);
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

/* The conversation while it is shown: its current line and highlighted
   response from the game, and on touch the panel, the span its line was
   wrapped into and each response's card where the last draw put them, with
   the record ids of the lines each leads to. */
void put_conversation(std::string *out,
                      const presentation::RetailScenePlane *plane) {
  namespace cv = native::conversation;
  const std::uint32_t self = RD32(native::exe_linked(cv::kSingleton));
  put_key(out, "conversation");
  if (self == 0u || (RD8(self + cv::kFlags) & cv::kVisible) == 0u) {
    out->append("null,");
    return;
  }
  const native::ConversationTouchDrawn &drawn =
      native::conversation_touch_drawn();
  out->push_back('{');
  put_int(out, "line_id", static_cast<long>(RD32(self + cv::kCurrentLine)));
  put_int(out, "highlighted",
          static_cast<std::int16_t>(RD16(self + cv::kTagIndex)));
  put_int(out, "response_count",
          static_cast<std::int16_t>(RD16(self + cv::kTagCount)));
  put_bool(out, "touch", drawn.shown);
  if (drawn.shown) {
    put_text(out, "speaker", drawn.speaker);
    put_text(out, "line", drawn.line);
    put_bool(out, "accepts", drawn.accepts);
    put_box_point(out, "panel_low", drawn.panel, 0.0F, 0.0F, plane);
    put_box_point(out, "panel_high", drawn.panel, 1.0F, 1.0F, plane);
    put_box_point(out, "text_low", drawn.text, 0.0F, 0.0F, plane);
    put_box_point(out, "text_high", drawn.text, 1.0F, 1.0F, plane);
  }
  put_key(out, "responses");
  out->push_back('[');
  for (std::size_t i = 0; drawn.shown && i < drawn.cards.size(); ++i) {
    const native::ConversationTouchCard &card = drawn.cards[i];
    out->push_back('{');
    put_box_tap(out, card.box, plane);
    put_box_point(out, "edge", card.box, 0.05F, 0.9F, plane);
    put_box_point(out, "low", card.box, 0.0F, 0.0F, plane);
    put_box_point(out, "high", card.box, 1.0F, 1.0F, plane);
    put_fingers(out, card.box, plane);
    put_text(out, "text", card.text);
    put_int(out, "tag", card.tag);
    put_int(out, "id", static_cast<long>(card.id));
    put_key(out, "next");
    out->push_back('[');
    for (const std::uint32_t id : card.next) {
      out->append(std::to_string(id));
      out->push_back(',');
    }
    if (out->back() == ',') {
      out->pop_back();
    }
    out->append("]},");
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
    /* A popup up in gameplay has no menu under it. */
    read->body = "{";
    put_bool(&read->body, "active", false);
    put_bool(&read->body, "popup", menu.popup_up);
    put_bool(&read->body, "text_input", native::win32_events_text_input_on());
    const auto plane = native::live_scene_plane();
    if (menu.popup) {
      put_popup(&read->body, *menu.popup, plane ? &*plane : nullptr);
    }
    put_conversation(&read->body, plane ? &*plane : nullptr);
    put_key(&read->body, "reason");
    put_string(&read->body, menu::read_status_name(read->status));
    read->body.append("}\n");
  }
}

} // namespace

const char *touch_press_name(input::TouchPress press) {
  switch (press) {
  case input::TouchPress::click:
    return "click";
  case input::TouchPress::own:
    return "own";
  }
  return "click";
}

/* The finger buttons the last frame drew over small items: each one's centre
   and a point near its top corner as taps, its height over a finger's, the
   scene point a press on it is delivered at and what the press hands the
   game. */
void put_touch_targets(std::string *out,
                       const presentation::RetailScenePlane *plane) {
  put_key(out, "touch_buttons");
  out->push_back('[');
  for (const input::TouchTarget &target : input::touch_targets().shown()) {
    out->push_back('{');
    put_box_tap(out, target.area, plane);
    put_box_point(out, "edge", target.area, 0.1F, 0.9F, plane);
    put_fingers(out, target.area, plane);
    put_scene_box(out, "area", target.area);
    if (target.name_text) {
      put_scene_box(out, "name_text", *target.name_text);
    }
    if (target.value_text) {
      put_scene_box(out, "value_text", *target.value_text);
    }
    put_float(out, "aim_x", target.aim.x);
    put_float(out, "aim_z", target.aim.z);
    put_text(out, "press", touch_press_name(target.press));
    if (!target.name.empty()) {
      put_text(out, "name", std::string(target.name));
    }
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
  put_bool(&out, "text_input", native::win32_events_text_input_on());
  if (menu.popup) {
    put_popup(&out, *menu.popup, plane);
  }
  put_conversation(&out, plane);
  if (menu.mode) {
    put_int(&out, "mode", static_cast<long>(*menu.mode));
  }
  if (menu.assigning_skill) {
    put_int(&out, "assigning_skill", static_cast<long>(*menu.assigning_skill));
  }
  if (menu.viewer) {
    put_key(&out, "viewer");
    out.push_back('{');
    put_int(&out, "review_mode", static_cast<long>(menu.viewer->review_mode));
    put_text(&out, "image", menu.viewer->image);
    close_object(&out);
    out.push_back(',');
  }
  if (menu.menu_class == "CMenuAutoMap") {
    put_automap(&out, menu.address);
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
  control_reply_json(fd, 200, "OK", body.c_str(), body.size());
}

namespace {

struct MenuPost {
  native::MenuAction action = native::MenuAction::ok;
  menu::ReadStatus status = menu::ReadStatus::ok;
  std::string menu_class;
};

void post_menu_body(void *context) {
  auto *post = static_cast<MenuPost *>(context);
  menu::MenuSnapshot menu;
  std::uint32_t failed = 0u;
  post->status = menu::read_live_menu(&menu, &failed);
  if (post->status != menu::ReadStatus::ok) {
    return;
  }
  post->menu_class = menu.menu_class;
  native::post_menu_action(post->action);
}

} // namespace

void menu_action_route(x2::native::Socket fd, const char *query) {
  char name[32] = "";
  if (control_query_arg(query, "name", name, sizeof name) == 0) {
    control_reply_text(fd, 400, "Bad Request",
                       "GET /menu/action?name=<ok|back|accept|...>\n");
    return;
  }
  std::string token = "$MENU_";
  for (const char *at = name; *at != '\0'; ++at) {
    token.push_back(
        static_cast<char>(std::toupper(static_cast<unsigned char>(*at))));
  }
  const auto action = native::menu_token_action(token);
  if (!action) {
    control_reply_text(fd, 400, "Bad Request", "%s names no menu action\n",
                       token.c_str());
    return;
  }
  MenuPost post;
  post.action = *action;
  if (control_command_guest_read(post_menu_body, &post) < 0) {
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s, so "
                       "nothing was posted\n");
    return;
  }
  if (post.status != menu::ReadStatus::ok) {
    control_reply_text(fd, 409, "Conflict",
                       "no retail menu is up (%s), so nothing was posted\n",
                       menu::read_status_name(post.status));
    return;
  }
  control_reply_text(fd, 200, "OK", "%s posted to %s\n", token.c_str(),
                     post.menu_class.c_str());
}

} // namespace x2::control
