#ifndef X2_PORT_OPTION_ROWS_HPP
#define X2_PORT_OPTION_ROWS_HPP

#include "x86rt.h"

#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

/* The port's own settings as rows of the retail Options screen and of its
   two sub-pages (plan #203): the one place a port setting becomes a row.
   The derived menus, the rows' commands, their drawing and GET /menu all
   read this table. */
namespace x2::native {

enum class PortPage { Options, Controls, Bindings };

struct PortSubPage {
  PortPage page;
  /* The menu `openmenu` opens, derived from Options (derived_menus.cpp). */
  std::string_view menu;
  std::string_view title;
};
/* The two sub-pages Options' link rows open. */
const std::vector<PortSubPage> &port_sub_pages();

struct PortOptionRow {
  /* The row's item is "port_option_<key>". */
  std::string key;
  std::string label;
  /* Only where the host's window is not the display (desktop). */
  bool desktop_only;
  PortPage page;
};

/* The rows this host offers on every page, each page's in screen order. */
std::vector<const PortOptionRow *> port_option_rows();
std::vector<const PortOptionRow *> port_option_rows(PortPage page);
const PortOptionRow *port_option_row(std::string_view key);
/* The row an Options item named `item` stands for. */
const PortOptionRow *port_option_row_of_item(std::string_view item);
std::string port_option_item(const PortOptionRow &row);

/* Its label and value as the screen shows them. */
std::string port_option_label(const PortOptionRow &row);
std::string port_option_value(const PortOptionRow &row);

/* The player chose `row` on the guest thread, +1 its next value and -1
   the previous: a link row opens its page, a binding row waits for its
   key (+1) or takes the game's key back (-1), every other step is queued
   for port_options_service. */
void port_option_choose(const CPU &cpu, const PortOptionRow &row,
                        int direction);
/* Applies the queued steps between frames on the host's event pump, where
   a live resolution change may reset the device, and saves them. */
void port_options_service(SDL_Window *window);

} // namespace x2::native

#endif
