#ifndef X2_SETTINGS_H
#define X2_SETTINGS_H

#include <stdint.h>

#include "boot_mode.h"

#define X2_SETTINGS_PLAYERS 4u
#define X2_SETTINGS_KEYBOARD_PROFILES 4u
#define X2_SETTINGS_ROWS 42u
#define X2_SETTINGS_DEVICE_ID 64u
#define X2_SETTINGS_CONTROLLER_ASSIGNMENTS X2_SETTINGS_PLAYERS
#define X2_SETTINGS_UNASSIGNED (-1)

typedef enum {
    X2_WINDOW_WINDOWED = 0,
    X2_WINDOW_BORDERLESS,
    X2_WINDOW_FULLSCREEN
} X2WindowMode;

typedef struct {
    char id[X2_SETTINGS_DEVICE_ID];
    int8_t player;
} X2ControllerAssignment;

typedef struct {
    uint16_t keyboard[X2_SETTINGS_ROWS];
    uint8_t keyboard_set[X2_SETTINGS_ROWS];
} X2KeyboardProfile;

typedef struct {
    unsigned width;
    unsigned height;
    X2WindowMode window_mode;
    uint8_t dynamic_shadows;
    uint16_t shadow_resolution;
    /* Multiplier on every glyph the engine loads. 0 means AUTO: hold the
       share of the screen the text has at 800x600. See ui_text_scale.c. */
    float text_scale;
    X2BootMode boot_mode;
    /* Device-assignment grid: each row has one owner or is unassigned. P1 may
       own one row of each kind for hotswap. P2-P4 own one device total. */
    int8_t keyboard_player[X2_SETTINGS_KEYBOARD_PROFILES];
    X2ControllerAssignment controller[X2_SETTINGS_CONTROLLER_ASSIGNMENTS];
    X2KeyboardProfile keyboard_profile[X2_SETTINGS_KEYBOARD_PROFILES];
} X2Settings;

void x2_settings_defaults(X2Settings *settings);
int x2_settings_load(X2Settings *settings, const char *path,
                     char *why, int whyn);
int x2_settings_save(const X2Settings *settings, const char *path,
                     char *why, int whyn);
const char *x2_window_mode_name(X2WindowMode mode);
int x2_window_mode_parse(const char *text, X2WindowMode *mode);
int x2_settings_assign_keyboard(X2Settings *settings, unsigned profile,
                                int player);
int x2_settings_assign_controller(X2Settings *settings, const char *id,
                                  int player);
int x2_settings_controller_player(const X2Settings *settings, const char *id);
const char *x2_settings_player_controller(const X2Settings *settings,
                                          unsigned player);
int x2_settings_player_keyboard(const X2Settings *settings, unsigned player);

#endif
