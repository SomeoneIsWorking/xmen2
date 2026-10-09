#pragma once

/*
 * The seam between the pad INVENTORY (dinput_pad.cpp, which owns when a device
 * exists) and the pad SAMPLER (dinput_pad_sample.cpp, which owns what the guest
 * reads out of one). The inventory keeps its Pad record private; the sampler
 * needs only the handle, and needs to tell apart the two ways it can fail to
 * get one, because they are different defects that produce the same "not
 * pressed".
 */
namespace x2::native {

enum X2PadSlotState {
  X2_PAD_SLOT_EMPTY = 0, /* no device in that slot at all */
  X2_PAD_SLOT_NO_HANDLE, /* a device, but SDL gave us no gamepad handle */
  X2_PAD_SLOT_READY
};

} // namespace x2::native

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

namespace x2::native {

X2PadSlotState dinput_pad_handle(int pad, SDL_Gamepad **out);

} // namespace x2::native
#endif
