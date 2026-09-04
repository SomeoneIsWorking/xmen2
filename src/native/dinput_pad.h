/*
 * The gamepads this host can see, as DirectInput needs to describe them.
 *
 * Separate from dinput_device.c on purpose. That file implements the
 * IDirectInputDevice8 the game holds; this one owns the SDL side -- which pads
 * exist, what they are called, what GUID names each of them, and what their
 * sticks and buttons read right now. The split is what lets the SAME inventory
 * answer three different callers: DirectInput 8 (the exe's own controller
 * code), DirectInput 7 (the engine's igWin32ControllerManager) and, when it
 * lands, hotswap -- a pad appearing has to reach all of them, and it can only
 * do that from one place that knows.
 *
 * "Pad index" here is a slot in THIS inventory, stable while a pad stays
 * connected, and is not SDL's joystick id and not the game's player number.
 */
#ifndef DINPUT_PAD_H
#define DINPUT_PAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DINPUT_PAD_MAX 8

/*
 * DirectInput's joystick axes and the game's own numbering for them.
 *
 * The names are DIJOYSTATE2's fields, because that is what the game asked for:
 * XMen2.exe's data format at 0x006a6514 is c_dfDIJoystick2 -- 272 bytes over
 * 164 objects -- read out of the exe rather than assumed.
 */
enum {
  DINPUT_PAD_AXIS_X = 0, /* left stick X   */
  DINPUT_PAD_AXIS_Y,     /* left stick Y   */
  DINPUT_PAD_AXIS_Z,     /* triggers, combined -- see dinput_pad.c */
  DINPUT_PAD_AXIS_RX,    /* right stick X  */
  DINPUT_PAD_AXIS_RY,    /* right stick Y  */
  DINPUT_PAD_AXIS_RZ,
  DINPUT_PAD_AXIS_COUNT
};

/* Rescan, opening pads that appeared and closing pads that went away. Safe to
   call every frame; it only does work when SDL reports a change. */
void dinput_pad_refresh(void);

/* Monotonic inventory generation. It advances exactly when a pad is opened or
   closed, including when a new device reuses a vacated inventory slot. */
uint64_t dinput_pad_generation(void);

/* How many pads are connected. 0 is a real answer, not an error. */
int dinput_pad_count(void);

/* Slot -> identity. Return 0 when the slot holds no pad. */
int dinput_pad_instance_guid(int pad, unsigned char guid[16]);
int dinput_pad_product_guid(int pad, unsigned char guid[16]);
const char *dinput_pad_name(int pad);

/* Host identity candidate for player assignment. DirectInput's instance GUID is
   deliberately only a live-run handle: two identical controllers have the
   same SDL device GUID, so that GUID cannot identify either physical unit.
   This id prefers the controller serial, then its OS path. Devices exposing
   neither receive a live-session id which must not be saved as a reservation.
 */
const char *dinput_pad_persistent_id(int pad);
int dinput_pad_persistent_id_is_stable(int pad);
int dinput_pad_for_persistent_id(const char *id);

/* Which slot that instance GUID names, or -1. This is how a CreateDevice for
   a GUID an enumeration handed out finds its way back to a pad. */
int dinput_pad_for_guid(const unsigned char guid[16]);

/* Which slot holds the pad with this live SDL joystick id, or -1. A caller
   that attached a joystick itself knows only that id, and needs the inventory
   slot to reason about player assignment. */
int dinput_pad_for_joystick_id(unsigned int joystick_id);

/* The transport's live identity used by shared engine adapters. Zero means no
   connected device and is never a valid identity. */
uint32_t dinput_pad_device_id(int pad);

/*
 * Live state. Axes come back in DirectInput's own signed range as the GAME
 * asked for it -- XMen2.exe sets every axis to [-1000, 1000] through
 * DIPROP_RANGE (FUN_00628510) -- so the caller passes the range it was given
 * and gets a value already scaled into it, rather than scaling a raw SDL value
 * itself in three different places.
 */
int32_t dinput_pad_axis(int pad, int axis, int32_t lo, int32_t hi);
int dinput_pad_button(int pad, int button); /* 0 or 1 */
int dinput_pad_button_count(int pad);
/* Independent trigger pressure survives beside DirectInput's intentionally
   combined Z axis so a native Alchemy controller keeps the richer state. */
float dinput_pad_trigger_pressure(int pad, int trigger);

/* Prompt-family selection follows the connected device, not the DirectInput
   layout we present to the 2005 game. SDL classifies Xbox 360/One mappings;
   every other family keeps the game's text names. The type predicate is
   exposed so the negative class is testable without owning two controllers. */
int dinput_pad_type_uses_xbox_glyphs(int sdl_gamepad_type);
int dinput_pad_uses_xbox_glyphs(int pad);
/* The d-pad as a DirectInput POV: hundredths of a degree clockwise from north,
   or 0xFFFFFFFF for centred, which is what DIJOYSTATE2's rgdwPOV holds. */
uint32_t dinput_pad_pov(int pad);

/* X2_VIRTUAL_PAD: attach a synthetic gamepad so a headless run can exercise
   the whole controller path. Announced loudly -- a run with a synthetic pad
   must not be mistakable for a run with real hardware. */
void dinput_pad_virtual_from_env(void);

/* Drives X2_VIRTUAL_PAD's frame-scheduled forms (attach at frame N, unplug at
   M). Called once a frame; an int compare until the frame arrives. */
void dinput_pad_virtual_tick(unsigned long frame);

void dinput_pad_report(void);

/*
 * Press a button ("a", "start", ...) or set an axis ("leftx", -1.0..1.0) on
 * the SYNTHETIC pad, held for `hold` seconds (button: 0 = default press,
 * negative = until explicit release; axis: 0 = until changed). Returns 0 with
 * a reason in `why`.
 *
 * SDL's virtual joystick reads zero everywhere until something sets it, so
 * without this a run with X2_VIRTUAL_PAD proved the game FINDS a pad and never
 * that a press reaches it. Names come from the same table the SDL mapping is
 * generated from.
 */
int dinput_pad_virtual_set(const char *what, double value, double hold,
                           char *why, int whyn);

/* Read-back through the OPEN gamepad of `pad`, for the synthetic pad's
   "did the set actually land" verification: 1 down/nonzero, 0 released,
   -1 no open gamepad. The enum is SDL's SDL_GAMEPAD_BUTTON or SDL_GAMEPAD_AXIS
   value as an int. */
int dinput_pad_open_gamepad_button(int pad, int gamepad_button);
int dinput_pad_open_gamepad_axis(int pad, int gamepad_axis);

/* Did the game ASK for pad state, and did any answer come back pressed?
   Reported with its denominator: "0 of 0" and "0 of 480000" are different
   findings and silence cannot distinguish them. */
void dinput_pad_poll_report(void);

/* Refresh SDL's latched pad state. Call ONCE per device poll, before
   reading axes and buttons -- they report what SDL last latched, and only
   this refreshes it. The keyboard and mouse paths have always done the
   equivalent; the pad path did not, so every button read came back
   released. */
void dinput_pad_refresh_state(void);

#ifdef __cplusplus
}
#endif

#endif /* DINPUT_PAD_H */
