#ifndef X2_TOUCH_RUNTIME_H
#define X2_TOUCH_RUNTIME_H

#include "../presentation/touch_layout.h"

#include <stddef.h>
#include <stdint.h>

union SDL_Event;
struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

/* SDL contact acquisition and publication into the existing virtual DirectInput
 * pad, on every platform: a desktop touchscreen and a phone reach this the same
 * way. Title layout remains owned by TouchControls. */
void x2_touch_runtime_window(struct SDL_Window *window);

typedef struct X2TouchPointer {
  int valid;
  float x;
  float y;
  int button_change; /* 1 = press, 0 = release, -1 = motion only. */
  uint32_t time_ms;
} X2TouchPointer;

int x2_touch_runtime_event(const union SDL_Event *event);

void x2_touch_runtime_lifecycle_event(const union SDL_Event *event);
/* Why a held zone is being let go. Counted apart, because one number
   reported as three causes it had never observed is what hid the real one. */
typedef enum {
  X2_TOUCH_CANCEL_OVERLAY_HIDDEN,
  X2_TOUCH_CANCEL_WINDOW_GONE,
  X2_TOUCH_CANCEL_SOURCE_CHANGED,
  X2_TOUCH_CANCEL_WINDOW_CHANGED
} X2TouchCancelCause;

void x2_touch_runtime_cancel(void);
void x2_touch_runtime_cancel_because(X2TouchCancelCause cause);

/* Copies the HUD's drawn tap regions (X2HudRegions). Null clears them all.
 * Visibility/layout changes release captured HUD contacts, never the stick or
 * the action buttons. */
void x2_touch_runtime_hud_regions(const X2HudRegions *regions);
/* 1 once per tap of the port menu button, for the UI that owns the menu. */
int x2_touch_runtime_take_menu_request(void);
/* The window event owner drains all portrait transitions in FIFO order,
 * including cancellation when no SDL event is pending. */
int x2_touch_runtime_take_pointer(X2TouchPointer *pointer);

/* What the overlay is being asked to draw. */
typedef enum {
  X2_TOUCH_VISUAL_BUTTON = 0,
  X2_TOUCH_VISUAL_STICK
} X2TouchVisualKind;

typedef struct X2TouchVisual {
  uint32_t id;
  float left;
  float top;
  float right;
  float bottom;
  int action;
  int active;
  int kind;
  /* A stick's live deflection, -1..1 per axis and never outside the unit
     circle, so the ring can draw where its thumb has pushed it. Zero for
     every other kind. A stick that does not show its deflection tells the
     player nothing about what the game is being sent. */
  float deflect_x;
  float deflect_y;
  /* A power button's cell in x2_power_slots_atlas(); -1 for every other
     kind. */
  int power_icon;
} X2TouchVisual;

/* The viewport the touch layout is currently built from -- the window's pixel
 * size and its safe area. Exposed so the HUD relocation and the control zones
 * are laid out from ONE viewport rather than each fetching its own idea of the
 * output size; the settings' width/height and the window's pixel size are not
 * always the same number, and a HUD placed against one while the zones are
 * placed against the other is the two-sources-of-truth bug again.
 *
 * Returns 0 when there is no window, leaving *out untouched. */
int x2_touch_runtime_viewport(X2LayoutViewport *out);

size_t x2_touch_runtime_visuals(X2TouchVisual *out, size_t capacity);

/* The name of a visual's `action`, for the control channel's listing. */
const char *x2_touch_runtime_action_name(int action);

/* The cinematic's Skip button, in output pixels, while one is drawn: touch
   play, a window, and a skip offered by the cutscene player
   (cutscene_skip.h). Returns 0 when there is none; `held` says a finger is
   on it. Its own document draws it, because the gameplay overlay is
   deliberately hidden while a cinematic holds the controls. */
int x2_touch_runtime_skip_button(X2Rect *rect, int *held);

/* IS TOUCH THE INPUT THE PLAYER IS USING RIGHT NOW?
 *
 * One answer, two consumers: the on-screen pad and the mobile HUD placement.
 * They are one feature -- the HUD moves to leave room for the thumbstick and
 * the buttons -- so a HUD that relocates while no pad is drawn is the HUD
 * making room for nothing, and the two deciding separately is the
 * two-sources-of-truth bug the viewport comment above already names.
 *
 * The platform is not the answer. A phone player on a controller wants
 * neither, and a desktop or tablet player on a touchscreen wants both. So the
 * answer is observed: it becomes yes at the first contact and no again at the
 * next keyboard, mouse or controller event, with the setting able to force
 * either end (X2_TOUCH_CONTROLS_OFF/ALWAYS).
 *
 * The overlay additionally requires a window and gameplay control; the HUD
 * placement asks this one, because it is laid out before the frame that would
 * report that control. */
int x2_touch_runtime_active(void);

/* This run's account of the feature, for the every-ending roll-call and for
   the periodic heartbeat -- `tag` says which, in the style of the engine's
   "[HB] ". The counts and their text are owned by touch_census.h.

   A browser run never ends, so the roll-call alone would leave the web
   product with no account of touch at all. */
void x2_touch_runtime_report(const char *tag);

/* Records which kind of device produced an event. Every host event goes past
 * here, including the ones touch never handles -- that is how a key press
 * puts the on-screen pad away. */
void x2_touch_runtime_note_source(const union SDL_Event *event);
int x2_touch_runtime_overlay_visible(void);

/* Is there anything for the overlay document to draw at all? The gameplay
   controls answer x2_touch_runtime_overlay_visible; the menu pad is drawn on
   exactly the screens where that is false, so a document shown only on the
   first answer would draw no menu pad anywhere. */
int x2_touch_runtime_has_visuals(void);

/* The atlas cell of each of the player's four RT powers, in the game's slot
   order (A, B, X, Y), or -1 where the hero has none; that slot gets no button.
   Changing the set releases a held control, as a layout change does. */
#define X2_POWER_SLOTS 4
void x2_touch_runtime_power_slots(const int icons[X2_POWER_SLOTS]);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_RUNTIME_H */
