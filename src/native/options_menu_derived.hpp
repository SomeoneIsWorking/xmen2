#ifndef X2_OPTIONS_MENU_DERIVED_HPP
#define X2_OPTIONS_MENU_DERIVED_HPP

#include <optional>
#include <string>

#include "xmlb_tree.hpp"

/* The retail Options menu with the port's rows added (plan #203), served
   by derived_menus.hpp in place of ui/menus/options.engb. */
namespace x2::native {

/* `retail`'s tree with one text item per port row after Accept, chained
   between Vibration and Accept for the pad, each running its row's step
   commands; nothing when `retail` is not the Options menu this expects. */
std::optional<xmlb::Node> derive_options_menu(const xmlb::Node &retail,
                                              std::string *why);

} // namespace x2::native

#endif
