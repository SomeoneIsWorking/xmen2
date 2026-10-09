#pragma once

#include "x86rt.h"

namespace x2::native {

/* Opt-in live reproduction for the Scourge Critter renderer defect. */
void entity_spawn_probe_after_script_launch(CPU *cpu, const char *script);

} // namespace x2::native
