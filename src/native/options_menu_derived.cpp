#include "options_menu_derived.hpp"

#include "options_menu.h"
#include "port_option_rows.hpp"

#include <vector>

namespace x2::native {
namespace {

constexpr std::string_view kAbove = "label_vibration";
constexpr std::string_view kBelow = "label_accept";

xmlb::Node *named(xmlb::Node &root, std::string_view name) {
  for (xmlb::Node &child : root.children) {
    if (child.name == "item" && child.get("name") == name) {
      return &child;
    }
  }
  return nullptr;
}

} // namespace

std::optional<xmlb::Node> derive_options_menu(const xmlb::Node &retail,
                                              std::string *why) {
  xmlb::Node root = retail;
  xmlb::Node *above = named(root, kAbove);
  xmlb::Node *below = named(root, kBelow);
  if (root.name != "MENU" || root.get("type") != "OPTIONS_MENU" ||
      above == nullptr || below == nullptr || above->get("down") != kBelow ||
      below->get("up") != kAbove) {
    *why = "no Vibration row directly above Accept";
    return std::nullopt;
  }
  const auto rows = port_option_rows();
  if (rows.empty()) {
    *why = "no port rows";
    return std::nullopt;
  }
  std::vector<xmlb::Node> items;
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const PortOptionRow &row = *rows[i];
    xmlb::Node item{"item", {}, {}};
    item.set("animtext_scene", "options");
    item.set("desctext2", "$MENU_ACCEPT Change");
    item.set("down", i + 1u < rows.size() ? port_option_item(*rows[i + 1u])
                                          : std::string(kBelow));
    item.set("leftcmd", port_option_command(-1));
    item.set("name", port_option_item(row));
    item.set("rightcmd", port_option_command(1));
    item.set("style", "STYLE_MENU_WHITE_MED");
    item.set("text", row.label);
    item.set("textalignx", "TEXT_ALIGN_LEFT");
    item.set("up",
             i > 0 ? port_option_item(*rows[i - 1u]) : std::string(kAbove));
    item.set("usecmd", port_option_command(1));
    items.push_back(std::move(item));
  }
  above->set("down", port_option_item(*rows.front()));
  below->set("up", port_option_item(*rows.back()));
  std::size_t at = 0;
  while (&root.children[at] != below) {
    ++at;
  }
  root.children.insert(root.children.begin() + static_cast<long>(at) + 1,
                       items.begin(), items.end());
  return root;
}

} // namespace x2::native
