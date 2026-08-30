#ifndef X2_TOUCH_RUNTIME_H
#define X2_TOUCH_RUNTIME_H

union SDL_Event;
struct SDL_Window;

#ifdef __cplusplus
extern "C" {
#endif

/* SDL/Android contact acquisition and publication into the existing virtual
 * DirectInput pad. Title layout remains owned by TouchControls. */
void x2_touch_runtime_window(struct SDL_Window *window);
int x2_touch_runtime_event(const union SDL_Event *event);
void x2_touch_runtime_cancel(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_RUNTIME_H */
