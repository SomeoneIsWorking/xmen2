#ifndef X2_OPTIONS_MENU_H
#define X2_OPTIONS_MENU_H

#include "x86rt.h"

/* Extends the retail menu-command registrar after it installs the authored
   table. The two retail Options callbacks remain unmodified. */
void x2_override_005f4900(CPU *C);

/* The additive BehavEd command emitted only by the derived pause menu. */
void x2_port_settings_command(CPU *C);

#endif
