#pragma once

#include "x86rt.h"

#include <string>

namespace x2::native {

/* Extends the retail menu-command registrar after it installs the authored
   table. The two retail Options callbacks remain unmodified. */
void override_005f4900(CPU *C);

/* The additive BehavEd command emitted only by the derived pause menu. */
void port_settings_command(CPU *C);

/* The command an Options port row runs to step the focused row by
   `direction`. */
std::string port_option_command(int direction);

} // namespace x2::native
