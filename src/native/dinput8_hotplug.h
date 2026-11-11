#pragma once

/* Hotswap: keeping the game's controller table in step with the live pad
   inventory. The game enumerates controllers once at startup and has no
   WM_DEVICECHANGE, so a pad that arrives later is admitted by calling the
   game's OWN enumeration routine again; the same routine re-derives the
   table after a save payload deserializes identities this session cannot
   have. See dinput8.cpp for the IDirectInput8 object it belongs to. */

struct X86pCpu;

namespace x2::native {

/* The game called EnumDevices(GAMECTRL) from `return_address`; admit the
   declared enumeration routine when that is its recovered call site, because
   that routine is what a later arrival has to be admitted through. Reports a
   caller that is not, naming the address it had. */
void dinput8_hotplug_note_game_enumeration(unsigned int return_address);

/* The guest's EnumDevices ran: the inventory generation it reported. */
void dinput8_hotplug_enumerated(unsigned long long generation, int connected,
                                int reported);

/* Called once a frame from the first input call; re-admits the inventory
   when its generation changed or the table stopped matching the live pads. */
void dinput8_hotplug_pump(struct X86pCpu *cpu);

/* How many admissions have run (shutdown report denominator). */
unsigned long dinput8_hotplug_admissions(void);

} // namespace x2::native
