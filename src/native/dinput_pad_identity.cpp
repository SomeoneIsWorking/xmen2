#include "dinput_pad_identity.h"
#include "prompt_glyph_style.h"

#include <stdio.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include "dinput_pad_virtual.h"
#include <SDL3/SDL.h>
#endif

int dinput_pad_type_uses_xbox_glyphs(int type) {
#ifdef X2_WITH_SDL
  /* Every pad SDL maps as a gamepad gets the port's art: the atlas has an
     Xbox, a PlayStation and a Nintendo style (prompt_glyph_style.h), and
     STANDARD -- SDL's type for a mapped pad it does not know by model, which
     covers most mobile controllers -- uses the Xbox face layout. */
  return type != SDL_GAMEPAD_TYPE_UNKNOWN;
#else
  (void)type;
  return 0;
#endif
}

int dinput_pad_type_button_style(int type) {
#ifdef X2_WITH_SDL
  switch (type) {
  case SDL_GAMEPAD_TYPE_PS3:
  case SDL_GAMEPAD_TYPE_PS4:
  case SDL_GAMEPAD_TYPE_PS5:
    return X2_PROMPT_STYLE_PLAYSTATION;
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
  case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
    return X2_PROMPT_STYLE_NINTENDO;
  default:
    break;
  }
#else
  (void)type;
#endif
  return X2_PROMPT_STYLE_XBOX;
}

namespace x2::native {

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

#ifdef X2_WITH_SDL
namespace {

uint64_t identity_hash(const char *tag, const char *value, const SDL_GUID *guid,
                       unsigned ordinal) {
  const unsigned char *s;
  uint64_t h = UINT64_C(1469598103934665603);
  size_t i;

#define HASH_BYTES(ptr, count)                                                 \
  do {                                                                         \
    const unsigned char *hb_ = (const unsigned char *)(ptr);                   \
    size_t hn_;                                                                \
    for (hn_ = 0; hn_ < (count); hn_++) {                                      \
      h ^= hb_[hn_];                                                           \
      h *= UINT64_C(1099511628211);                                            \
    }                                                                          \
  } while (0)
  for (s = (const unsigned char *)tag; *s; s++)
    HASH_BYTES(s, 1);
  if (value)
    for (s = (const unsigned char *)value; *s; s++)
      HASH_BYTES(s, 1);
  HASH_BYTES(guid->data, sizeof guid->data);
  for (i = 0; i < sizeof ordinal; i++)
    HASH_BYTES((unsigned char *)&ordinal + i, 1);
#undef HASH_BYTES
  return h;
}

} // namespace

void dinput_pad_make_identities(SDL_Gamepad *gp, SDL_JoystickID id,
                                const SDL_GUID *guid, const char *name,
                                unsigned model_ordinal, unsigned char inst[16],
                                char *persistent_id,
                                size_t persistent_id_size) {
  const char *serial = SDL_GetGamepadSerial(gp);
  const char *path = SDL_GetGamepadPath(gp);
  const char *tag;
  const char *value;
  unsigned ordinal;
  uint64_t stable;
  uint32_t live = (uint32_t)id;
  int i;

  /* The announced synthetic identity wins, so a run can drive the same
     persisted-assignment resolution a stored controller0 id goes through. */
  {
    const char *override = dinput_pad_virtual_identity_override(id);
    if (override) {
      snprintf(persistent_id, persistent_id_size, "%s", override);
      return;
    }
  }
  /* SDL's GUID is a PRODUCT identity, despite its name. Preserve its device
     description but mix in SDL's unique live instance id. For equal GUIDs,
     different joystick ids now produce different DirectInput instances. */
  memcpy(inst, guid->data, 16);
  for (i = 0; i < 4; i++) {
    uint32_t word;
    memcpy(&word, inst + i * 4, sizeof word);
    word ^= live * (UINT32_C(0x9e3779b9) + (uint32_t)i * UINT32_C(0x85ebca6b));
    memcpy(inst + i * 4, &word, sizeof word);
  }

  if (serial && serial[0]) {
    tag = "serial";
    value = serial;
    ordinal = 0;
  } else if (path && path[0]) {
    tag = "path";
    value = path;
    ordinal = 0;
  } else if (!SDL_IsJoystickVirtual(id)) {
    /* Android reports neither a serial nor a path for many USB and
       Bluetooth pads. Name + model is stable across runs, so a player's
       assignment can be saved instead of lasting one session. */
    tag = "product";
    value = name;
    ordinal = model_ordinal;
  } else {
    /* A session id distinguishes units but is not a physical identity. */
    tag = "session";
    value = name;
    ordinal = (unsigned)id;
  }
  stable = identity_hash(tag, value, guid, ordinal);
  snprintf(persistent_id, persistent_id_size,
           strcmp(tag, "session") ? "sdl-%04x-%04x-%016llx"
                                  : "sdl-session-%04x-%04x-%016llx",
           SDL_GetGamepadVendorForID(id), SDL_GetGamepadProductForID(id),
           (unsigned long long)stable);
}
#endif /* X2_WITH_SDL */

} // namespace x2::native
