#ifndef X2_SETTINGS_H
#define X2_SETTINGS_H

#include <stdint.h>

#define X2_SETTINGS_PLAYERS 4u
#define X2_SETTINGS_KEYBOARD_PROFILES 4u
#define X2_SETTINGS_ROWS 42u
#define X2_SETTINGS_DEVICE_ID 64u

typedef enum {
    X2_WINDOW_WINDOWED = 0,
    X2_WINDOW_BORDERLESS,
    X2_WINDOW_FULLSCREEN
} X2WindowMode;

typedef enum {
    X2_PLAYER_NONE = 0,
    X2_PLAYER_AUTO,
    X2_PLAYER_KEYBOARD,
    X2_PLAYER_GAMEPAD
} X2PlayerDevice;

typedef struct {
    X2PlayerDevice type;
    char id[X2_SETTINGS_DEVICE_ID];
    uint8_t keyboard_profile;
} X2PlayerSettings;

typedef struct {
    uint16_t keyboard[X2_SETTINGS_ROWS];
    uint8_t keyboard_set[X2_SETTINGS_ROWS];
} X2KeyboardProfile;

typedef struct {
    unsigned width;
    unsigned height;
    X2WindowMode window_mode;
    X2PlayerSettings player[X2_SETTINGS_PLAYERS];
    X2KeyboardProfile keyboard_profile[X2_SETTINGS_KEYBOARD_PROFILES];
} X2Settings;

void x2_settings_defaults(X2Settings *settings);
int x2_settings_load(X2Settings *settings, const char *path,
                     char *why, int whyn);
int x2_settings_save(const X2Settings *settings, const char *path,
                     char *why, int whyn);
const char *x2_window_mode_name(X2WindowMode mode);
int x2_window_mode_parse(const char *text, X2WindowMode *mode);
const char *x2_player_device_name(X2PlayerDevice device);

#endif
