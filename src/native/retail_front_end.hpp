#ifndef X2_RETAIL_FRONT_END_HPP
#define X2_RETAIL_FRONT_END_HPP

/*
 * The retail front end as a port owner drives it.
 *
 * XMen2.exe's menus chain themselves with console commands ("openmenu
 * online", "setuphost", "mainmenuexit 1") and act on the item a player has
 * focused when accept arrives. RetailFrontEnd exposes exactly those levers --
 * queue a console command, name the active menu, focus one of its items by
 * name -- so a port feature runs the game's own handlers instead of
 * reimplementing what they do.
 */

extern "C" {
#include "x86rt.h"
}

#include <cstdint>
#include <string>
#include <string_view>

namespace x2::retail {

class FrontEnd {
public:
  /* Queue on the console, as every retail menu handler does: the command runs
     at the console's own safe point, never inside the caller. False when the
     executable is not mapped or the console is shutting down. */
  static bool queue_command(const CPU &cpu, std::string_view command);

  /* The active menu object (a CMenu), or 0 when no menu is up. */
  static uint32_t active_menu_object(const CPU &cpu);

  /* The name the active menu was opened by ("online", "host", ...), or empty
     when no menu is up. */
  static std::string active_menu(const CPU &cpu);

  /* Focus the active menu's item called `item`, as a pointer or pad would.
     False when no menu is up or it has no such item. */
  static bool focus(const CPU &cpu, std::string_view item);

  /* Focus the open popup's option that runs `script`, as a pointer or pad
     would, so the next accept chooses it. False when no popup is open or none
     of its options runs that script. */
  static bool focus_popup_option(const CPU &cpu, std::string_view script);
};

} // namespace x2::retail

#endif
