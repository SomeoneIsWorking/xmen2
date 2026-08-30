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
extern double g_vaxis_until[X2_VIRTUAL_AXIS_COUNT];
extern short g_vaxis_value[X2_VIRTUAL_AXIS_COUNT];

int axis_is_trigger(int axis);
short trigger_raw(double value);
void virtual_expire(void);

#endif /* X2_DINPUT_PAD_VIRTUAL_INTERNAL_H */
