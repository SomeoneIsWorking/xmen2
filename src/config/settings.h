#pragma once

#include <cstdint>

#include "boot_mode.h"
#include "hud_settings.h"

namespace x2::config {

inline constexpr unsigned kSettingsPlayers = 4u;
inline constexpr unsigned kSettingsKeyboardProfiles = 4u;
inline constexpr unsigned kSettingsRows = 42u;
inline constexpr unsigned kSettingsDeviceId = 64u;
inline constexpr unsigned kSettingsControllerAssignments = kSettingsPlayers;
inline constexpr int kSettingsUnassigned = -1;

enum class WindowMode : int { Windowed = 0, Borderless, Fullscreen };

struct ControllerAssignment {
  char id[kSettingsDeviceId];
  int8_t player;
};

enum TouchControls : int {
  kTouchControlsOff = 0,
  kTouchControlsAuto = 1,
  kTouchControlsAlways = 2
};

const char *touch_controls_label(unsigned mode);

/* What reaching an extraction point does for a fallen or hurt party. */
enum class ExtractionRevive : int { Off = 0, Free, Paid };

const char *extraction_revive_name(ExtractionRevive mode);
const char *extraction_revive_label(ExtractionRevive mode);
int extraction_revive_parse(const char *text, ExtractionRevive *mode);

/* A row with keyboard_set clear follows the game's own binding. */
struct KeyboardProfile {
  uint16_t keyboard[kSettingsRows];
  uint8_t keyboard_set[kSettingsRows];
};

void keyboard_profile_restore_row(KeyboardProfile *profile, unsigned row);
void keyboard_profile_restore_all(KeyboardProfile *profile);

struct Settings {
  unsigned width;
  unsigned height;
  WindowMode window_mode;
  uint8_t dynamic_shadows;
  uint16_t shadow_resolution;
  /* Multiplier on every glyph the engine loads. 0 means AUTO: hold the
     share of the screen the text has at 800x600. See ui_text_scale.cpp. */
  float text_scale;
  BootMode boot_mode;
  /* Whether the on-screen touch pad and the mobile HUD placement it comes
     with are allowed on screen. AUTO is the default everywhere: neither the
     host platform nor a saved preference knows whether the player has a
     finger or a controller on the game right now, and only one of those two
     wants a thumbstick drawn over the HUD. ALWAYS is the desktop iteration
     path -- a layout nobody can look at without a phone gets shipped wrong.
     Persisted numerically, so an existing file's 0/1 keeps its meaning. */
  uint8_t touch_controls;
  /* gameplay.extraction_revive: off is the retail rule (a fallen hero stays
     down), free restores the party at the pad, paid offers the retail
     revive cost near it. */
  ExtractionRevive extraction_revive;
  X2HudSettings hud;
  /* Device-assignment grid: each row has one owner or is unassigned. P1 may
     own one row of each kind for hotswap. P2-P4 own one device total. */
  int8_t keyboard_player[kSettingsKeyboardProfiles];
  ControllerAssignment controller[kSettingsControllerAssignments];
  KeyboardProfile keyboard_profile[kSettingsKeyboardProfiles];
};

void settings_defaults(Settings *settings);
int settings_load(Settings *settings, const char *path, char *why, int whyn);
int settings_save(const Settings *settings, const char *path, char *why,
                  int whyn);
const char *window_mode_name(WindowMode mode);
int window_mode_parse(const char *text, WindowMode *mode);
int settings_assign_keyboard(Settings *settings, unsigned profile, int player);
int settings_assign_controller(Settings *settings, const char *id, int player);
int settings_controller_player(const Settings *settings, const char *id);
const char *settings_player_controller(const Settings *settings,
                                       unsigned player);
int settings_player_keyboard(const Settings *settings, unsigned player);

} // namespace x2::config
