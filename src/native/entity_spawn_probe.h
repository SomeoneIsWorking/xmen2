#ifndef X2_ENTITY_SPAWN_PROBE_H
#define X2_ENTITY_SPAWN_PROBE_H

#include "x86rt.h"

/* Opt-in live reproduction for the Scourge Critter renderer defect. */
void entity_spawn_probe_after_script_launch(CPU *cpu, const char *script);

#endif
