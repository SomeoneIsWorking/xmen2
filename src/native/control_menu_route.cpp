#include "control_menu_route.hpp"

#include "../input/touch_runtime_menu.hpp"
#include "control.h"
#include "control_query.h"
#include "json_string.h"

#include <cstdio>
#include <vector>

namespace x2::control {
namespace {

void put_string(std::string *out, const std::string &value) {
  std::vector<char> buffer(value.size() * 6u + 3u);
  json_string_format(buffer.data(), buffer.size(), value.c_str());
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
             const menu::MenuItem &item) {
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

void put_text_item(std::string *out, const menu::MenuItem &item) {
  out->push_back('{');
  put_int(out, "slot", static_cast<long>(item.slot));
  put_text(out, "name", item.name);
  put_text(out, "class", menu::item_class_name(item.item_class));
  put_text(out, "label", item.label);
  put_rect(out, item.rect);
  put_bool(out, "enabled", item.enabled());
  close_object(out);
}

void put_item(std::string *out, const menu::MenuItem &item) {
  out->push_back('{');
  put_int(out, "slot", static_cast<long>(item.slot));
  put_text(out, "name", item.name);
  put_text(out, "class", menu::item_class_name(item.item_class));
  put_text(out, "label", item.label);
  put_rect(out, item.rect);
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
  } else {
    put_text(out, "label", state.view.rows[index].label);
  }
  put_float(out, "left", button.rect.left);
  put_float(out, "top", button.rect.top);
  put_float(out, "width", button.rect.right - button.rect.left);
  put_float(out, "height", button.rect.bottom - button.rect.top);
  close_object(out);
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
  put_key(&out, "rows");
  out.push_back('[');
  for (const input::TouchMenuRow &row : state.view.rows) {
    out.push_back('{');
    put_text(&out, "label", row.label);
    put_text(&out, "value", row.value);
    put_fill(&out, row.fill);
    put_bool(&out, "focused", row.focused);
    put_bool(&out, "steps", row.steps);
    close_object(&out);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.append("],");
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

std::string menu_json(const menu::MenuSnapshot &menu, bool all_items) {
  std::string out = "{";
  put_bool(&out, "active", true);
  put_text(&out, "menu", menu.name);
  put_text(&out, "class", menu.menu_class);
  char address[16];
  std::snprintf(address, sizeof address, "0x%08x", menu.address);
  put_text(&out, "address", address);
  put_bool(&out, "popup", menu.popup_up);
  if (menu.mode) {
    put_int(&out, "mode", static_cast<long>(*menu.mode));
  }
  put_int(&out, "item_count", static_cast<long>(menu.items.size()));
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
    put_row(&out, menu, menu.items[static_cast<std::size_t>(row)]);
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
    put_text_item(&out, item);
    out.push_back(',');
  }
  if (out.back() == ',') {
    out.pop_back();
  }
  out.push_back(']');
  if (all_items) {
    out.append(",\"items\":[");
    for (const menu::MenuItem &item : menu.items) {
      put_item(&out, item);
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

void menu_route(x2_socket_t fd, const char *query) {
  char items[8] = "";
  const bool all_items =
      control_query_arg(query, "items", items, sizeof items) != 0 &&
      std::string(items) == "all";
  menu::MenuSnapshot menu;
  std::uint32_t failed = 0;
  const menu::ReadStatus status = menu::read_live_menu(&menu, &failed);
  if (status == menu::ReadStatus::unreadable) {
    control_reply_text(fd, 500, "Internal Server Error",
                       "the menu model could not be read: guest address "
                       "0x%08x is unreadable\n",
                       failed);
    return;
  }
  std::string body;
  if (status == menu::ReadStatus::ok) {
    body = menu_json(menu, all_items);
  } else {
    body = menu.popup_up ? "{\"active\":false,\"popup\":true,\"reason\":"
                         : "{\"active\":false,\"popup\":false,\"reason\":";
    put_string(&body, menu::read_status_name(status));
    body.append("}\n");
  }
  body.resize(body.size() - 2u);
  body.append(",\"touch_menu\":");
  body.append(touch_menu_json(input::touch_menu_state()));
  body.append("}\n");
  control_reply_json(fd, 200, "OK", body.c_str(), body.size());
}

} // namespace x2::control
