#include "dinput_pad_identity.h"

#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

int dinput_pad_type_uses_xbox_glyphs(int type) {
#ifdef X2_WITH_SDL
  return type == SDL_GAMEPAD_TYPE_XBOX360 || type == SDL_GAMEPAD_TYPE_XBOXONE;
#else
  (void)type;
  return 0;
#endif
}

void dinput_pad_make_product_guid(unsigned char guid[16], uint16_t vendor,
                                  uint16_t product) {
  static const unsigned char tail[10] = {0x00, 0x00, 0x00, 0x00, 'P',
                                         'I',  'D',  'V',  'I',  'D'};
  guid[0] = (unsigned char)(vendor & 0xff);
  guid[1] = (unsigned char)(vendor >> 8);
  guid[2] = (unsigned char)(product & 0xff);
  guid[3] = (unsigned char)(product >> 8);
  memcpy(guid + 4, tail, sizeof tail);
  guid[14] = 0x00;
  guid[15] = 0x00;
}
