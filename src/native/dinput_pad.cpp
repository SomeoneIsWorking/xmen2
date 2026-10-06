#include "x2_log.h"
/*
 * The gamepad inventory: SDL3 on one side, DirectInput's idea of a joystick on
 * the other. See dinput_pad.h for why this is its own file.
 *
 * WHAT THE GAME ASKED FOR, read out of XMen2.exe rather than assumed. The
 * enumeration callback is FUN_00628b40 and it does, in order:
 *
 *   CreateDevice(guidInstance)          the GUID an enumeration handed it
 *   copy 100 bytes from instance+0x28   the instance NAME, kept per slot
 *   SetDataFormat(0x006a6514)           c_dfDIJoystick2: 272 bytes, 164 objects
 *   SetCooperativeLevel(hwnd, 5 or 6)   EXCLUSIVE or NONEXCLUSIVE | FOREGROUND
 *   EnumObjects(FUN_00628b20, ctx, 3)   DIDFT_AXIS -- once per axis
 *
 * and its per-axis callback (FUN_00628510) immediately calls
 * SetProperty(DIPROP_RANGE, {dwSize 0x18, dwHeaderSize 0x10, dwObj = the
 * object's dwType, dwHow = DIPH_BYID, lMin = -1000, lMax = +1000}).
 *
 * So the axis range is the GAME's, not DirectInput's default, and a host that
 * returned 0..65535 would hand it sticks pinned hard right. That is why
 * dinput_pad_axis takes the range it must produce.
 *
 * The host reports no DIDOI_FFACTUATOR flag because it has no force feedback.
 *
 * WHAT A PAD LOOKS LIKE. It is presented as the DirectInput layout of an Xbox
 * 360 pad, a layout the game's controller-type enumeration already names.
 * Left stick is X/Y,
 * right stick on Rx/Ry, both triggers COMBINED on Z (left positive, right
 * negative -- the 360's actual DirectInput behaviour, not a simplification),
 * d-pad on POV 0, and ten buttons in the 360's order.
 */
#include "dinput_pad.h"

#include "dinput_pad_identity.h"
#include "dinput_pad_internal.h"
#include "dinput_pad_report.h"
#include "dinput_pad_virtual.h"
#include "guest_clock.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef X2_WITH_SDL
#include <SDL3/SDL.h>
#endif

typedef struct {
  int used;
#ifdef X2_WITH_SDL
  SDL_Gamepad *gp;
  SDL_JoystickID id;
#endif
  unsigned char inst[16]; /* unique live-run DirectInput identity */
  unsigned char prod[16]; /* product GUID -- the PIDVID form */
  char name[128];
  char persistent_id[64];
  int buttons;
  int xbox_glyphs;
} Pad;

static Pad g_pad[DINPUT_PAD_MAX];
static int g_scanned;
static unsigned long g_opens, g_closes;
static uint64_t g_generation;
#ifdef X2_WITH_SDL

static int slot_of_id(SDL_JoystickID id) {
  int i;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used && g_pad[i].id == id)
      return i;
  return -1;
}

static uint64_t identity_hash(const char *tag, const char *value,
                              const SDL_GUID *guid, unsigned ordinal) {
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

static void make_identities(Pad *p, SDL_Gamepad *gp, SDL_JoystickID id,
                            const SDL_GUID *guid, unsigned ordinal) {
  const char *serial = SDL_GetGamepadSerial(gp);
  const char *path = SDL_GetGamepadPath(gp);
  const char *tag;
  const char *value;
  uint64_t stable;
  uint32_t live = (uint32_t)id;
  int i;

  /* The announced synthetic identity wins, so a run can drive the same
     persisted-assignment resolution a stored controller0 id goes through. */
  {
    const char *override = dinput_pad_virtual_identity_override(id);
    if (override) {
      snprintf(p->persistent_id, sizeof p->persistent_id, "%s", override);
      return;
    }
  }
  /* SDL's GUID is a PRODUCT identity, despite its name. Preserve its device
     description but mix in SDL's unique live instance id. For equal GUIDs,
     different joystick ids now produce different DirectInput instances. */
  memcpy(p->inst, guid->data, sizeof p->inst);
  for (i = 0; i < 4; i++) {
    uint32_t word;
    memcpy(&word, p->inst + i * 4, sizeof word);
    word ^= live * (UINT32_C(0x9e3779b9) + (uint32_t)i * UINT32_C(0x85ebca6b));
    memcpy(p->inst + i * 4, &word, sizeof word);
  }

  if (serial && serial[0]) {
    tag = "serial";
    value = serial;
    ordinal = 0;
  } else if (path && path[0]) {
    tag = "path";
    value = path;
    ordinal = 0;
  } else {
    /* A session id distinguishes units but is not a physical identity. */
    tag = "session";
    value = p->name;
    ordinal = (unsigned)id;
  }
  stable = identity_hash(tag, value, guid, ordinal);
  snprintf(p->persistent_id, sizeof p->persistent_id,
           strcmp(tag, "session") ? "sdl-%04x-%04x-%016llx"
                                  : "sdl-session-%04x-%04x-%016llx",
           SDL_GetGamepadVendorForID(id), SDL_GetGamepadProductForID(id),
           (unsigned long long)stable);
}

static void pad_open(SDL_JoystickID id) {
  int i;
  SDL_Gamepad *gp;
  Pad *p;
  SDL_GUID gu;
  const char *nm;

  if (slot_of_id(id) >= 0)
    return;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (!g_pad[i].used)
      break;
  if (i == DINPUT_PAD_MAX) {
    x2_log_error("DINPUT-PAD: a %d-th pad appeared and there is no slot "
                 "for it. The game supports four players; the limit here "
                 "is DINPUT_PAD_MAX in dinput_pad.h.\n",
                 DINPUT_PAD_MAX + 1);
    return;
  }
  if (!(gp = SDL_OpenGamepad(id))) {
    x2_log_error("DINPUT-PAD: SDL_OpenGamepad(%u) failed (%s); this pad "
                 "is NOT enumerated rather than enumerated and dead.\n",
                 (unsigned)id, SDL_GetError());
    return;
  }
  p = &g_pad[i];
  memset(p, 0, sizeof *p);
  p->used = 1;
  p->gp = gp;
  p->id = id;
  gu = SDL_GetJoystickGUIDForID(id);
  dinput_pad_make_product_guid(p->prod, SDL_GetGamepadVendorForID(id),
                               SDL_GetGamepadProductForID(id));
  nm = SDL_GetGamepadNameForID(id);
  snprintf(p->name, sizeof p->name, "%s", nm ? nm : "Gamepad");
  make_identities(p, gp, id, &gu, (unsigned)i);
  /* The DirectInput button order is the fixed 360 mapping below. */
  p->buttons = 10;
  p->xbox_glyphs =
      dinput_pad_type_uses_xbox_glyphs((int)SDL_GetGamepadType(gp));
  g_opens++;
  g_generation++;
  x2_log_error("DINPUT-PAD: pad %d connected -- \"%s\" (vendor 0x%04x "
               "product 0x%04x). Presented to the game as an Xbox 360 "
               "DirectInput pad: 6 axes, 10 buttons, 1 POV.\n",
               i, p->name, SDL_GetGamepadVendorForID(id),
               SDL_GetGamepadProductForID(id));
}

static void pad_close(SDL_JoystickID id) {
  int i = slot_of_id(id);
  if (i < 0)
    return;
  SDL_CloseGamepad(g_pad[i].gp);
  x2_log_error("DINPUT-PAD: pad %d disconnected -- \"%s\".\n", i,
               g_pad[i].name);
  memset(&g_pad[i], 0, sizeof g_pad[i]);
  g_closes++;
  g_generation++;
}
#endif /* X2_WITH_SDL */

uint64_t dinput_pad_generation(void) { return g_generation; }

void dinput_pad_refresh(void) {
#ifdef X2_WITH_SDL
  int n = 0, i, j;
  SDL_JoystickID *ids;

  if (!dinput_pad_subsystem_start()) {
    /* Said ONCE, and it matters: with no gamepad subsystem every
       enumeration below reports zero pads, which is indistinguishable
       from a machine with no pad plugged in. */
    static int told;
    if (!told++)
      x2_log_error("DINPUT-PAD: SDL_INIT_GAMEPAD failed (%s). NO "
                   "pad can be enumerated in this run -- that is "
                   "the subsystem missing, not an empty USB "
                   "port.\n",
                   SDL_GetError());
    return;
  }
  if (!(ids = SDL_GetGamepads(&n)))
    return;
  for (i = 0; i < n; i++)
    pad_open(ids[i]);
  /* And close anything SDL no longer lists. Done by rescan rather than by
     event so that this is correct however it is called -- an event-only path
     misses every pad that was already gone when the first poll happened. */
  for (i = 0; i < DINPUT_PAD_MAX; i++) {
    if (!g_pad[i].used)
      continue;
    for (j = 0; j < n; j++)
      if (ids[j] == g_pad[i].id)
        break;
    if (j == n)
      pad_close(g_pad[i].id);
  }
  SDL_free(ids);
  g_scanned = 1;
#endif
}

int dinput_pad_count(void) {
  int i, n = 0;
  if (!g_scanned)
    dinput_pad_refresh();
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used)
      n++;
  return n;
}

static Pad *pad_at(int pad) {
  if (pad < 0 || pad >= DINPUT_PAD_MAX || !g_pad[pad].used)
    return NULL;
  return &g_pad[pad];
}

#ifdef X2_WITH_SDL
X2PadSlotState dinput_pad_handle(int pad, SDL_Gamepad **out) {
  Pad *p = pad_at(pad);
  if (!p) {
    return X2_PAD_SLOT_EMPTY;
  }
  if (!p->gp) {
    return X2_PAD_SLOT_NO_HANDLE;
  }
  *out = p->gp;
  return X2_PAD_SLOT_READY;
}
#endif

int dinput_pad_instance_guid(int pad, unsigned char guid[16]) {
  Pad *p = pad_at(pad);
  if (!p)
    return 0;
  memcpy(guid, p->inst, 16);
  return 1;
}

int dinput_pad_product_guid(int pad, unsigned char guid[16]) {
  Pad *p = pad_at(pad);
  if (!p)
    return 0;
  memcpy(guid, p->prod, 16);
  return 1;
}

const char *dinput_pad_name(int pad) {
  Pad *p = pad_at(pad);
  return p ? p->name : NULL;
}

const char *dinput_pad_persistent_id(int pad) {
  Pad *p = pad_at(pad);
  return p ? p->persistent_id : NULL;
}
int dinput_pad_persistent_id_is_stable(int pad) {
  Pad *p = pad_at(pad);
  return p && strncmp(p->persistent_id, "sdl-session-", 12) != 0;
}
int dinput_pad_for_persistent_id(const char *id) {
  int i;
  if (!id || !id[0])
    return -1;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used && dinput_pad_persistent_id_is_stable(i) &&
        strcmp(g_pad[i].persistent_id, id) == 0)
      return i;
  return -1;
}
int dinput_pad_for_guid(const unsigned char guid[16]) {
  int i;
  for (i = 0; i < DINPUT_PAD_MAX; i++)
    if (g_pad[i].used && memcmp(g_pad[i].inst, guid, 16) == 0)
      return i;
  return -1;
}

int dinput_pad_for_joystick_id(unsigned int joystick_id) {
#ifdef X2_WITH_SDL
  return slot_of_id((SDL_JoystickID)joystick_id);
#else
  (void)joystick_id;
  return -1;
#endif
}

uint32_t dinput_pad_device_id(int pad) {
#ifdef X2_WITH_SDL
  Pad *p = pad_at(pad);
  return p ? (uint32_t)p->id : 0;
#else
  (void)pad;
  return 0;
#endif
}

int dinput_pad_button_count(int pad) {
  Pad *p = pad_at(pad);
  return p ? p->buttons : 0;
}

int dinput_pad_uses_xbox_glyphs(int pad) {
  Pad *p = pad_at(pad);
  return p ? p->xbox_glyphs : 0;
}

void dinput_pad_device_counts(unsigned long *opens, unsigned long *closes) {
  *opens = g_opens;
  *closes = g_closes;
}

int dinput_pad_describe(int slot, const char **name, int *buttons,
                        int *xbox_glyphs) {
  if (slot < 0 || slot >= DINPUT_PAD_MAX || !g_pad[slot].used) {
    return 0;
  }
  *name = g_pad[slot].name;
  *buttons = g_pad[slot].buttons;
  *xbox_glyphs = g_pad[slot].xbox_glyphs;
  return 1;
}
