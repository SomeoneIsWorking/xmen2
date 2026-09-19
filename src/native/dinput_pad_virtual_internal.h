#ifndef X2_DINPUT_PAD_VIRTUAL_INTERNAL_H
#define X2_DINPUT_PAD_VIRTUAL_INTERNAL_H

#include <SDL3/SDL.h>

#define X2_VIRTUAL_BUTTON_COUNT 10
#define X2_VIRTUAL_AXIS_COUNT 6

extern int g_virt_at_frame;
extern SDL_JoystickID g_virt_id;
extern SDL_Joystick *g_virt_js;
extern int g_virt_detach_at;
extern char g_virtual_persistent_id[64];
extern unsigned long g_vbtn_clears;
extern unsigned long g_vpad_presses;
extern unsigned long g_vpad_axis_sets;
extern const char *const g_vbtn_name[X2_VIRTUAL_BUTTON_COUNT];
extern const char *const g_vaxis_name[X2_VIRTUAL_AXIS_COUNT];
extern double g_vbtn_until[X2_VIRTUAL_BUTTON_COUNT];
/* The game's button- and axis-read counters at the moment each was set, and
   whether a release is waiting for the game to look. A press the game never
   read is a press that did not happen; see virtual_expire. */
extern unsigned long g_vbtn_reads_at_set[X2_VIRTUAL_BUTTON_COUNT];
extern unsigned long g_vaxis_reads_at_set[X2_VIRTUAL_AXIS_COUNT];
extern int g_vbtn_release_pending[X2_VIRTUAL_BUTTON_COUNT];
extern int g_vaxis_release_pending[X2_VIRTUAL_AXIS_COUNT];
extern unsigned long g_vpad_releases_deferred;
/* The longest a deferred release may wait for a poll that never comes. */
#define X2_VIRTUAL_RELEASE_CEILING_S 0.30
extern double g_vaxis_until[X2_VIRTUAL_AXIS_COUNT];
extern short g_vaxis_value[X2_VIRTUAL_AXIS_COUNT];

int axis_is_trigger(int axis);
short trigger_raw(double value);
void virtual_expire(void);
/* Build and open the synthetic SDL device. Idempotent only in the sense that
   its callers check g_virt_id first; it does not check for itself. */
void virtual_attach(void);

#endif /* X2_DINPUT_PAD_VIRTUAL_INTERNAL_H */
