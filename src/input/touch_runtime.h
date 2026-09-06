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

/* SDL/Android contact acquisition and publication into the existing virtual
 * DirectInput pad. Title layout remains owned by TouchControls. */
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
void x2_touch_runtime_cancel(void);
/* Copies the HUD's output-pixel portrait bounds. Null or mask zero clears
 * them. Visibility/layout changes release captured portrait pointers. */
void x2_touch_runtime_hud_regions(const X2Rect portraits[4],
                                  unsigned visible_mask);
/* The window event owner drains all portrait transitions in FIFO order,
 * including cancellation when no SDL event is pending. */
int x2_touch_runtime_take_pointer(X2TouchPointer *pointer);

typedef struct X2TouchVisual {
  uint32_t id;
  float left;
  float top;
  float right;
  float bottom;
  int action;
  int active;
  int stick;
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

/* IS TOUCH THE INPUT THE PLAYER IS USING RIGHT NOW?
 *
 * One answer, two consumers: the on-screen pad and the mobile HUD placement.
 * They are one feature -- the HUD moves to leave room for the thumbstick and
 * the buttons -- so a HUD that relocates while no pad is drawn is the HUD
 * making room for nothing, and the two deciding separately is the
 * two-sources-of-truth bug the viewport comment above already names.
 *
 * The platform is not the answer. An Android player on a controller wants
 * neither, and a desktop player on a touchscreen wants both. So the answer is
 * observed: it becomes yes at the first contact and no again at the next
 * keyboard, mouse or controller event, with the setting able to force either
 * end (X2_TOUCH_CONTROLS_OFF/ALWAYS).
 *
 * The overlay additionally requires a window and gameplay control; the HUD
 * placement asks this one, because it is laid out before the frame that would
 * report that control. */
int x2_touch_runtime_active(void);
/* Records which kind of device produced an event. Every host event goes past
 * here, including the ones touch never handles -- that is how a key press
 * puts the on-screen pad away. */
void x2_touch_runtime_note_source(const union SDL_Event *event);
int x2_touch_runtime_overlay_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_RUNTIME_H */
