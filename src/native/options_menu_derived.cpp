#include "options_menu_derived.hpp"

#include "host_files.h"
#include "options_menu.h"
#include "port_option_rows.hpp"
#include "shell32.h"
#include "win_path.h"
#include "x2_log.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

namespace x2::native {
namespace {

constexpr std::string_view kMenuStem = "ui/menus/options.";
constexpr std::string_view kAbove = "label_vibration";
constexpr std::string_view kBelow = "label_accept";
constexpr std::string_view kDerivedDir = "derived";

xmlb::Node *named(xmlb::Node &root, std::string_view name) {
  for (xmlb::Node &child : root.children) {
    if (child.name == "item" && child.get("name") == name) {
      return &child;
    }
  }
  return nullptr;
}

/* "ui/menus/options.engb" from any spelling of it; empty for other files. */
std::string options_menu_name(const char *guest_path) {
  std::string path;
  if (guest_path[0] != '\0' && guest_path[1] == ':') {
    guest_path += 2;
  }
  while (*guest_path == '\\' || *guest_path == '/') {
    ++guest_path;
  }
  for (; *guest_path != '\0'; ++guest_path) {
    const char c = *guest_path == '\\' ? '/' : *guest_path;
    path.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (path.size() != kMenuStem.size() + 4u ||
      path.compare(0, kMenuStem.size(), kMenuStem) != 0) {
    return {};
  }
  const std::string_view ext = std::string_view(path).substr(kMenuStem.size());
  /* xmlb, or a language's own: engb, frgb, gegb, ... */
  return ext == "xmlb" || ext.substr(2) == "gb" ? path : std::string();
}

bool read_file(const char *path, std::vector<std::uint8_t> *out) {
  FILE *file = host_fopen(path, "rb");
  if (file == nullptr) {
    return false;
  }
  std::uint8_t chunk[16384];
  std::size_t got = 0;
  while ((got = std::fread(chunk, 1, sizeof chunk, file)) > 0) {
    out->insert(out->end(), chunk, chunk + got);
  }
  const bool ok = std::ferror(file) == 0;
  std::fclose(file);
  return ok;
}

bool make_dirs(const std::string &path) {
  for (std::size_t at = 1; at <= path.size(); ++at) {
    if (at == path.size() || path[at] == '/') {
      const std::string part = path.substr(0, at);
      if (host_mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) {
        return false;
      }
    }
  }
  return true;
}

bool write_file(const std::string &path,
                const std::vector<std::uint8_t> &data) {
  const std::string pending = path + ".new";
  FILE *file = host_fopen(pending.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const bool written =
      std::fwrite(data.data(), 1, data.size(), file) == data.size();
  const bool closed = std::fclose(file) == 0;
  return written && closed && host_replace(pending.c_str(), path.c_str()) == 0;
}

/* The derived file for `name`, written on first use; empty when it cannot
   be made, and then the install's own file is opened. */
std::string derived_path(const char *guest_path, const std::string &name) {
  const char *saves = save_dir();
  const char *retail = win_path(guest_path);
  std::vector<std::uint8_t> bytes;
  std::string why;
  if (saves == nullptr || saves[0] == '\0' || retail == nullptr ||
      !read_file(retail, &bytes)) {
    x2_log_error("OPTIONS MENU: cannot read \"%s\" to add the port's rows; "
                 "Options opens without them.\n",
                 guest_path);
    return {};
  }
  const auto tree = xmlb::parse(bytes);
  const auto derived =
      tree ? derive_options_menu(*tree, &why) : std::optional<xmlb::Node>();
  if (!derived) {
    x2_log_error("OPTIONS MENU: \"%s\" is not the Options menu the port's "
                 "rows extend (%s); Options opens without them.\n",
                 retail, tree ? why.c_str() : "not complete XMLB");
    return {};
  }
  const std::string path =
      std::string(saves) + "/" + std::string(kDerivedDir) + "/" + name;
  if (!make_dirs(path.substr(0, path.rfind('/'))) ||
      !write_file(path, xmlb::serialise(*derived))) {
    x2_log_error("OPTIONS MENU: cannot write %s: %s; Options opens without "
                 "the port's rows.\n",
                 path.c_str(), std::strerror(errno));
    return {};
  }
  x2_log_info("OPTIONS MENU: %s is %s with the port's rows.\n", path.c_str(),
              retail);
  return path;
}

/* Each menu file's derived path, made once per run; empty when it failed. */
std::map<std::string, std::string> g_derived;

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

const char *options_menu_replacement(const char *guest_path) {
  if (guest_path == nullptr) {
    return nullptr;
  }
  const std::string name = options_menu_name(guest_path);
  if (name.empty()) {
    return nullptr;
  }
  auto found = g_derived.find(name);
  if (found == g_derived.end()) {
    found = g_derived.emplace(name, derived_path(guest_path, name)).first;
  }
  return found->second.empty() ? nullptr : found->second.c_str();
}

} // namespace x2::native

__attribute__((constructor)) static void
x2_options_menu_derived_register(void) {
  k32_set_asset_deriver(x2::native::options_menu_replacement);
}
