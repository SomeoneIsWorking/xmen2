#ifndef X2_OPTIONS_MENU_DERIVED_HPP
#define X2_OPTIONS_MENU_DERIVED_HPP

#include <optional>
#include <string>
#include <string_view>

#include "xmlb_tree.hpp"

/* The retail Options menu with the port's rows added (plan #203). The menu
   file the game opens (ui/menus/options.engb, or another language's) is
   derived from the player's own install into the save directory on first
   open and served in its place, so every platform gets the same rows. */
namespace x2::native {

/* `retail`'s tree with one text item per port row after Accept, chained
   between Vibration and Accept for the pad, each running its row's step
   commands; nothing when `retail` is not the Options menu this expects. */
std::optional<xmlb::Node> derive_options_menu(const xmlb::Node &retail,
                                              std::string *why);

/* The host path to open for `guest_path` instead of the install's file, or
   null when the port derives nothing for it. */
const char *options_menu_replacement(const char *guest_path);

} // namespace x2::native

#endif
