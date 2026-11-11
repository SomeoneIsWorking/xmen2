#include "derived_menus.hpp"

#include "friend_code_rows.hpp"
#include "host_files.h"
#include "options_menu_derived.hpp"
#include "shell32.h"
#include "win_path.h"
#include "x2_log.h"

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <string_view>
#include <vector>

namespace x2::native {
namespace {

struct DerivedMenu {
  std::string_view stem;
  std::string_view title;
  std::optional<xmlb::Node> (*derive)(const xmlb::Node &retail,
                                      std::string *why);
};

constexpr std::array<DerivedMenu, 2> kMenus{{
    {"ui/menus/options.", "Options", derive_options_menu},
    {"ui/menus/campaign_lobby.", "the online lobby", derive_lobby_menu},
}};
constexpr std::string_view kDerivedDir = "derived";

/* The menu `guest_path` names, as "ui/menus/options.engb" from any spelling
   of it, and its entry; nothing for other files. */
std::optional<std::pair<std::string, const DerivedMenu *>>
menu_of(const char *guest_path) {
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
  for (const DerivedMenu &menu : kMenus) {
    if (path.size() != menu.stem.size() + 4u ||
        path.compare(0, menu.stem.size(), menu.stem) != 0) {
      continue;
    }
    const std::string_view ext =
        std::string_view(path).substr(menu.stem.size());
    /* xmlb, or a language's own: engb, frgb, gegb, ... */
    if (ext == "xmlb" || ext.substr(2) == "gb") {
      return std::pair{path, &menu};
    }
  }
  return std::nullopt;
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
std::string derived_path(const char *guest_path, const std::string &name,
                         const DerivedMenu &menu) {
  const char *saves = save_dir();
  const char *retail = win_path(guest_path);
  std::vector<std::uint8_t> bytes;
  std::string why;
  const std::string title(menu.title);
  if (saves == nullptr || saves[0] == '\0' || retail == nullptr ||
      !read_file(retail, &bytes)) {
    x2_log_error("DERIVED MENU: cannot read \"%s\" to add the port's rows; "
                 "%s opens without them.\n",
                 guest_path, title.c_str());
    return {};
  }
  const auto tree = xmlb::parse(bytes);
  const auto derived =
      tree ? menu.derive(*tree, &why) : std::optional<xmlb::Node>();
  if (!derived) {
    x2_log_error("DERIVED MENU: \"%s\" is not the menu the port's rows "
                 "extend (%s); %s opens without them.\n",
                 retail, tree ? why.c_str() : "not complete XMLB",
                 title.c_str());
    return {};
  }
  const std::string path =
      std::string(saves) + "/" + std::string(kDerivedDir) + "/" + name;
  if (!make_dirs(path.substr(0, path.rfind('/'))) ||
      !write_file(path, xmlb::serialise(*derived))) {
    x2_log_error("DERIVED MENU: cannot write %s: %s; %s opens without the "
                 "port's rows.\n",
                 path.c_str(), std::strerror(errno), title.c_str());
    return {};
  }
  x2_log_info("DERIVED MENU: %s is %s with the port's rows.\n", path.c_str(),
              retail);
  return path;
}

/* Each menu file's derived path, made once per run; empty when it failed. */
std::map<std::string, std::string> g_derived;

} // namespace

const char *derived_menu_replacement(const char *guest_path) {
  if (guest_path == nullptr) {
    return nullptr;
  }
  const auto menu = menu_of(guest_path);
  if (!menu) {
    return nullptr;
  }
  auto found = g_derived.find(menu->first);
  if (found == g_derived.end()) {
    found = g_derived
                .emplace(menu->first,
                         derived_path(guest_path, menu->first, *menu->second))
                .first;
  }
  return found->second.empty() ? nullptr : found->second.c_str();
}

} // namespace x2::native

__attribute__((constructor)) static void x2_derived_menus_register(void) {
  k32_set_asset_deriver(x2::native::derived_menu_replacement);
}
