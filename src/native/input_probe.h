#ifndef X2_INPUT_PROBE_H
#define X2_INPUT_PROBE_H

#include <stddef.h>

struct CPU;

/*
 * A snapshot of the GAME's input state, not the host's.
 *
 * dinput_pad.c can say whether SDL and DirectInput delivered a button; only
 * this can say whether the game's own binding table turned it into an action.
 * The two questions look the same from a log and are one layer apart, and the
 * gap between them is where a press that arrives still does nothing.
 *
 * `controller` chooses whose binding table is printed row by row. It matters:
 * the game keeps four master sets (0..3, what the options UI edits and the
 * registry persists) and copies them into the working sets it actually
 * evaluates (4..7) and the menu sets (12..15), so a report of set 0 alone can
 * show a binding the running game never consults.
 *
 * Returns the number of bytes written. It always writes something: an empty
 * report would be indistinguishable from "the probe never ran".
 */
size_t input_probe_report(struct CPU *cpu, unsigned controller,
                          char *out, size_t n);

#endif
