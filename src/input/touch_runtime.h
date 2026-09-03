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

int x2_touch_runtime_event(const union SDL_Event *event,
                           X2TouchPointer *pointer);
void x2_touch_runtime_lifecycle_event(const union SDL_Event *event);
void x2_touch_runtime_cancel(void);

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
int x2_touch_runtime_overlay_visible(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_RUNTIME_H */
