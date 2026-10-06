#pragma once

#include <string>

namespace x2::ui {

/* Escape text that will be placed inside generated RML. Settings values come
   from device names, keyboard labels and title strings, none of which are
   under this port's control, so none may be concatenated into markup raw. */
std::string escape_rml(const std::string &text);

} // namespace x2::ui
