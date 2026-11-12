#ifndef X2_OPTIONS_MENU_DERIVED_HPP
#define X2_OPTIONS_MENU_DERIVED_HPP

#include <optional>
#include <string>

#include "port_option_rows.hpp"
#include "xmlb_tree.hpp"

/* The retail Options menu with the port's rows added, and its sub-pages
   (plan #203), served by derived_menus.hpp in place of the install's
   files. */
namespace x2::native {

/* `retail`'s tree with one text item per Options row after Accept, chained
   between Vibration and Accept for the pad, each running its row's step
   commands. Options is stacked, so Back from a page it opens returns to
   it. Nothing when `retail` is not the Options menu this expects. */
std::optional<xmlb::Node> derive_options_menu(const xmlb::Node &retail,
                                              std::string *why);
/* Options reduced to its title, footer and Accept, titled for `page`,
   holding that page's rows chained from Accept. */
std::optional<xmlb::Node> derive_options_page(const xmlb::Node &retail,
                                              PortPage page, std::string *why);

} // namespace x2::native

#endif
