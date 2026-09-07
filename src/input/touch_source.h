#ifndef X2_TOUCH_SOURCE_H
#define X2_TOUCH_SOURCE_H

/*
 * WHICH KIND OF DEVICE IS THE PLAYER USING RIGHT NOW?
 *
 * Only touch and not-touch are distinguished, because only one decision
 * depends on it: whether the on-screen pad and the mobile HUD placement that
 * comes with it belong on screen. The platform cannot answer that -- a phone
 * player on a controller wants neither, a Windows tablet or 2-in-1 player on a
 * touchscreen wants both -- so it is observed from the host event stream.
 * Touch is a DEVICE, not a package: nothing here is Android's.
 *
 * This lives apart from touch_runtime so the classification can be exercised
 * without a window, a virtual pad or a running game.
 */

union SDL_Event;

#ifdef __cplusplus
extern "C" {
#endif

/* Classify one host event. Events from every other device kind are ignored,
   not counted as not-touch: a controller being plugged in is not the player
   picking it up. */
void x2_touch_source_note(const union SDL_Event *event);
int x2_touch_source_is_touch(void);
/* Tests and a re-entered window own no history. */
void x2_touch_source_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_SOURCE_H */
