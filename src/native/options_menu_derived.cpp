#include "options_menu_derived.hpp"

#include "options_menu.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <vector>

namespace x2::native {
namespace {

constexpr std::string_view kAbove = "label_vibration";
constexpr std::string_view kAccept = "label_accept";
constexpr std::string_view kTitle = "label_options";
constexpr std::string_view kNoStack = "no_stack";
/* Focused on open; Options' own is on Effects Volume, absent from a page. */
constexpr std::string_view kStartActive = "startactive";
/* What a page keeps of Options: the frame, the title, the footer and
   Accept. The settings, their art and the controller page are dropped,
   and an item that is not in the file draws nothing. */
constexpr std::array<std::string_view, 19> kPageItems{"track01",
                                                      "track02",
                                                      "helpbar",
                                                      "helpbar_hinge_left",
                                                      "helpbar_hinge_right",
                                                      "desctext1",
                                                      "desctext2",
                                                      "desctext3",
                                                      "desctext4",
                                                      "desctext5",
                                                      "title_bracket_top",
                                                      "title",
                                                      "title_light",
                                                      "title_focus",
                                                      "accept",
                                                      "accept_light",
                                                      "accept_focus",
                                                      "label_options",
                                                      "label_accept"};

xmlb::Node *named(xmlb::Node &root, std::string_view name) {
  for (xmlb::Node &child : root.children) {
    if (child.name == "item" && child.get("name") == name) {
      return &child;
    }
  }
  return nullptr;
}

bool is_options(xmlb::Node &root) {
  return root.name == "MENU" && root.get("type") == "OPTIONS_MENU" &&
         named(root, kAccept) != nullptr && named(root, kTitle) != nullptr;
}

/* Items for `rows`, chained from `above` to `below`. */
std::vector<xmlb::Node>
row_items(const std::vector<const PortOptionRow *> &rows,
          std::string_view above, std::string_view below) {
  std::vector<xmlb::Node> items;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const PortOptionRow &row = *rows[i];
    xmlb::Node item{"item", {}, {}};
    item.set("animtext_scene", "options");
    item.set("desctext2", "$MENU_ACCEPT Change");
    item.set("down", i + 1u < rows.size() ? port_option_item(*rows[i + 1u])
                                          : std::string(below));
    item.set("leftcmd", port_option_command(-1));
    item.set("name", port_option_item(row));
    item.set("rightcmd", port_option_command(1));
    item.set("style", "STYLE_MENU_WHITE_MED");
    item.set("text", row.label);
    item.set("textalignx", "TEXT_ALIGN_LEFT");
    item.set("up",
             i > 0 ? port_option_item(*rows[i - 1u]) : std::string(above));
    item.set("usecmd", port_option_command(1));
    items.push_back(std::move(item));
  }
  return items;
}

void insert_after_accept(xmlb::Node &root, std::vector<xmlb::Node> items) {
  const auto at = std::find_if(
      root.children.begin(), root.children.end(), [](const xmlb::Node &child) {
        return child.name == "item" && child.get("name") == kAccept;
      });
  root.children.insert(at + 1, std::make_move_iterator(items.begin()),
                       std::make_move_iterator(items.end()));
}

} // namespace

std::optional<xmlb::Node> derive_options_menu(const xmlb::Node &retail,
                                              std::string *why) {
  xmlb::Node root = retail;
  xmlb::Node *above = named(root, kAbove);
  xmlb::Node *accept = named(root, kAccept);
  if (!is_options(root) || above == nullptr || above->get("down") != kAccept ||
      accept->get("up") != kAbove) {
    *why = "no Vibration row directly above Accept";
    return std::nullopt;
  }
  const auto rows = port_option_rows(PortPage::Options);
  if (rows.empty()) {
    *why = "no port rows";
    return std::nullopt;
  }
  above->set("down", port_option_item(*rows.front()));
  accept->set("up", port_option_item(*rows.back()));
  std::erase_if(root.attrs,
                [](const auto &attr) { return attr.first == kNoStack; });
  insert_after_accept(root, row_items(rows, kAbove, kAccept));
  return root;
}

std::optional<xmlb::Node> derive_options_page(const xmlb::Node &retail,
                                              PortPage page, std::string *why) {
  xmlb::Node root = retail;
  const auto sub = std::find_if(
      port_sub_pages().begin(), port_sub_pages().end(),
      [page](const PortSubPage &known) { return known.page == page; });
  const auto rows = port_option_rows(page);
  if (!is_options(root) || sub == port_sub_pages().end() || rows.empty()) {
    *why = "not the Options menu a page is made from";
    return std::nullopt;
  }
  std::erase_if(root.children, [](const xmlb::Node &child) {
    if (child.name != "item") {
      return false;
    }
    const auto name = child.get("name");
    return !name || std::find(kPageItems.begin(), kPageItems.end(), *name) ==
                        kPageItems.end();
  });
  named(root, kTitle)->set("text", sub->title);
  xmlb::Node *accept = named(root, kAccept);
  accept->set("up", port_option_item(*rows.back()));
  accept->set("down", port_option_item(*rows.front()));
  auto items = row_items(rows, kAccept, kAccept);
  items.front().set(kStartActive, "true");
  insert_after_accept(root, std::move(items));
  return root;
}

} // namespace x2::native
