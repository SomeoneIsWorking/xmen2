#ifndef X2_OPTIONS_MENU_H
#define X2_OPTIONS_MENU_H

#include "x86rt.h"

/* BehavEd callbacks registered by the retail executable as `options` and
   `options_main`. Public for the production-seam regression test. */
void x2_override_005f1c50(CPU *C);
void x2_override_005f1fa0(CPU *C);

#endif
