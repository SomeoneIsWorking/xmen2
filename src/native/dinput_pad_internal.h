#ifndef X2_DINPUT_PAD_INTERNAL_H
#define X2_DINPUT_PAD_INTERNAL_H

/*
 * The seam between the pad INVENTORY (dinput_pad.c, which owns when a device
 * exists) and the pad SAMPLER (dinput_pad_sample.c, which owns what the guest
 * reads out of one). The inventory keeps its Pad record private; the sampler
 * needs only the handle, and needs to tell apart the two ways it can fail to
 * get one, because they are different defects that produce the same "not
 * pressed".
 */
typedef enum {
  X2_PAD_SLOT_EMPTY = 0, /* no device in that slot at all */
  X2_PAD_SLOT_NO_HANDLE, /* a device, but SDL gave us no gamepad handle */
  X2_PAD_SLOT_READY
} X2PadSlotState;

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
X2PadSlotState dinput_pad_handle(int pad, SDL_Gamepad **out);
#endif

#endif
