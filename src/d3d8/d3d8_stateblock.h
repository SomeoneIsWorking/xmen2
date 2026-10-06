/*
 * D3D8 state blocks.
 *
 * A state block is a snapshot of the device's state that the engine can replay
 * with one call. D3D8 has two ways to make one:
 *
 *   CreateStateBlock(type, &token)   captures the CURRENT state, immediately,
 *                                    for one of three fixed categories
 *   BeginStateBlock() ... EndStateBlock(&token)
 *                                    records the state the guest SETS in
 *                                    between, and nothing else
 *
 * and then ApplyStateBlock / CaptureStateBlock / DeleteStateBlock over the
 * token.
 *
 * Only the first is implemented here. The recording form needs the device's
 * setters to write into a block instead of the device -- a different mechanism,
 * not a bigger version of this one -- and the vtable slots for it stay NULL, so
 * the engine reaching them is reported BY NAME rather than silently doing
 * nothing. Half of a recording block is worse than none: it would apply an
 * empty snapshot over live state.
 *
 * A block is a whole copy of D3D8State. That is exact for D3DSBT_ALL, which is
 * what this game asks for. D3DSBT_PIXELSTATE and D3DSBT_VERTEXSTATE capture
 * documented SUBSETS, and applying more than the engine asked for would clobber
 * state it expected to keep -- so those are refused by name rather than
 * approximated with a full copy.
 */
#ifndef D3D8_STATEBLOCK_H
#define D3D8_STATEBLOCK_H

#include <stdint.h>

#include "d3d8_state.h"

/* D3DSTATEBLOCKTYPE */
#define D3DSBT_ALL 1u
#define D3DSBT_PIXELSTATE 2u
#define D3DSBT_VERTEXSTATE 3u

/*
 * Capture `now` into a new block and hand back its token.
 *
 * Returns 0 and says why on an unsupported type or a full table. The token is
 * an index with a generation counter in its high bits, so a token that has
 * been deleted -- or was never one -- reports itself instead of addressing
 * whatever took its slot.
 */
int d3d8_sb_create(uint32_t type, const D3D8State *now, uint32_t *token_out);

/* Re-capture the current state into an existing block (D3D8's
   CaptureStateBlock). 0 and a report if the token is not a live block. */
int d3d8_sb_capture(uint32_t token, const D3D8State *now);

/* Replay a block over the device state. 0 and a report if the token is not a
   live block -- never a silent no-op, which would leave the device in a state
   the engine believes it has replaced. */
int d3d8_sb_apply(uint32_t token, D3D8State *dst);

int d3d8_sb_delete(uint32_t token);

/* Blocks made, applied, captured, deleted, and how many are still live at
   shutdown. A block that is never applied means the engine's state is not
   being restored, which shows as drawing that is wrong later and nowhere
   near here. */
void d3d8_sb_report(void);

#endif /* D3D8_STATEBLOCK_H */
