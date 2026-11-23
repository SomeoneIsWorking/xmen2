#pragma once

#include <cstdint>

namespace x2::native {

/* Build the DirectInput PIDVID product identity presented to the guest. */
void dinput_pad_make_product_guid(unsigned char guid[16], uint16_t vendor,
                                  uint16_t product);

} // namespace x2::native

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>

#include <cstddef>

namespace x2::native {

/* One opened pad's live DirectInput instance GUID (`inst`) and the persistent
   id player assignments are saved under: its serial, else its OS path, else
   (real hardware exposing neither, common on Android) its name and model
   numbered by `model_ordinal` among identical connected units. Virtual pads
   get a live-session id that must not be saved. */
void dinput_pad_make_identities(SDL_Gamepad *gp, SDL_JoystickID id,
                                const SDL_GUID *guid, const char *name,
                                unsigned model_ordinal, unsigned char inst[16],
                                char *persistent_id,
                                std::size_t persistent_id_size);

} // namespace x2::native
#endif
