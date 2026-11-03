#ifndef X2_PORT_OPTION_ROWS_HPP
#define X2_PORT_OPTION_ROWS_HPP

#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

/* The port's own settings as rows of the retail Options screen (plan #203):
   the one place a port setting becomes a row. The derived options menu, the
   rows' commands, their drawing and GET /menu all read this table. */
namespace x2::native {

struct PortOptionRow {
  /* The row's item is "port_option_<key>". */
  std::string_view key;
  std::string_view label;
  /* Only where the host's window is not the display (desktop). */
  bool desktop_only;
};

/* The rows this host offers, in screen order. */
std::vector<const PortOptionRow *> port_option_rows();
const PortOptionRow *port_option_row(std::string_view key);
/* The row an Options item named `item` stands for. */
const PortOptionRow *port_option_row_of_item(std::string_view item);
std::string port_option_item(const PortOptionRow &row);

/* Its value as the screen shows it. */
std::string port_option_value(const PortOptionRow &row);

/* One step of `row`, +1 to the next value and -1 to the previous, queued
   from a guest command and applied by port_options_service. */
void port_option_request(const PortOptionRow &row, int direction);
/* Applies the queued steps between frames on the host's event pump, where
   a live resolution change may reset the device, and saves them. */
void port_options_service(SDL_Window *window);

} // namespace x2::native

#endif
