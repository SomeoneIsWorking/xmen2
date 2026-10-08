#include "settings.h"
#include "input_assignments.h"
#include "platform_posix.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define X2_MIN_WIDTH 640u
#define X2_MAX_WIDTH 7680u
#define X2_MIN_HEIGHT 480u
#define X2_MAX_HEIGHT 4320u

namespace x2::config {

namespace {
void reason(char *why, int whyn, const char *text) {
  if (why && whyn > 0)
    snprintf(why, (size_t)whyn, "%s", text);
}
} // namespace

const char *window_mode_name(WindowMode mode) {
  static const char *const NAME[] = {"windowed", "borderless", "fullscreen"};
  const auto index = static_cast<unsigned>(mode);
  return index <= static_cast<unsigned>(WindowMode::Fullscreen) ? NAME[index]
                                                                : "invalid";
}

int window_mode_parse(const char *text, WindowMode *mode) {
  int i;
  for (i = static_cast<int>(WindowMode::Windowed);
       i <= static_cast<int>(WindowMode::Fullscreen); i++)
    if (strcmp(text, window_mode_name(static_cast<WindowMode>(i))) == 0) {
      if (mode)
        *mode = static_cast<WindowMode>(i);
      return 1;
    }
  return 0;
}

const char *touch_controls_label(unsigned mode) {
  switch (mode) {
  case kTouchControlsOff:
    return "Off";
  case kTouchControlsAlways:
    return "Always";
  default:
    return "Automatic";
  }
}

const char *extraction_revive_name(ExtractionRevive mode) {
  switch (mode) {
  case ExtractionRevive::Free:
    return "free";
  case ExtractionRevive::Paid:
    return "paid";
  default:
    return "off";
  }
}

const char *extraction_revive_label(ExtractionRevive mode) {
  switch (mode) {
  case ExtractionRevive::Free:
    return "Free full restore";
  case ExtractionRevive::Paid:
    return "Paid revive";
  default:
    return "Off";
  }
}

int extraction_revive_parse(const char *text, ExtractionRevive *mode) {
  for (int i = static_cast<int>(ExtractionRevive::Off);
       i <= static_cast<int>(ExtractionRevive::Paid); i++) {
    const auto candidate = static_cast<ExtractionRevive>(i);
    if (strcmp(text, extraction_revive_name(candidate)) == 0) {
      if (mode)
        *mode = candidate;
      return 1;
    }
  }
  return 0;
}

void keyboard_profile_restore_row(KeyboardProfile *profile, unsigned row) {
  if (!profile || row >= kSettingsRows)
    return;
  profile->keyboard[row] = 0;
  profile->keyboard_set[row] = 0;
}

void keyboard_profile_restore_all(KeyboardProfile *profile) {
  if (profile)
    memset(profile, 0, sizeof *profile);
}

void settings_defaults(Settings *settings) {
  unsigned i;
  memset(settings, 0, sizeof *settings);
  settings->width = 1280;
  settings->height = 720;
  settings->window_mode = WindowMode::Windowed;
  settings->dynamic_shadows = 1;
  settings->shadow_resolution = 2048;
  settings->text_scale = 0.0f; /* auto */
  settings->boot_mode = BootMode::Normal;
  settings->touch_controls = kTouchControlsAuto;
  settings->extraction_revive = ExtractionRevive::Off;
  x2_hud_settings_defaults(&settings->hud);
  for (i = 0; i < kSettingsKeyboardProfiles; i++)
    settings->keyboard_player[i] = kSettingsUnassigned;
  for (i = 0; i < kSettingsControllerAssignments; i++)
    settings->controller[i].player = kSettingsUnassigned;
  settings->keyboard_player[0] = 0;
}

namespace {
char *trim(char *s) {
  char *end;
  while (isspace((unsigned char)*s))
    s++;
  end = s + strlen(s);
  while (end > s && isspace((unsigned char)end[-1]))
    *--end = 0;
  return s;
}

int number(const char *text, unsigned min, unsigned max, unsigned *out) {
  char *end;
  unsigned long value;
  errno = 0;
  value = strtoul(text, &end, 10);
  if (errno || end == text || *end || value < min || value > max)
    return 0;
  *out = (unsigned)value;
  return 1;
}

typedef enum {
  LEGACY_NONE,
  LEGACY_AUTO,
  LEGACY_KEYBOARD,
  LEGACY_GAMEPAD
} LegacyDevice;

typedef struct {
  LegacyDevice device;
  char id[kSettingsDeviceId];
  unsigned profile;
} LegacyPlayer;

typedef struct {
  Settings settings;
  LegacyPlayer legacy_player[kSettingsPlayers];
  int saw_legacy;
  int saw_grid;
} ParseState;

int parse_legacy_device(LegacyPlayer *player, const char *value) {
  if (strcmp(value, "none") == 0)
    player->device = LEGACY_NONE;
  else if (strcmp(value, "auto") == 0)
    player->device = LEGACY_AUTO;
  else if (strcmp(value, "keyboard") == 0)
    player->device = LEGACY_KEYBOARD;
  else if (strncmp(value, "gamepad:", 8) == 0 && value[8]) {
    if (strlen(value + 8) >= sizeof player->id)
      return 0;
    player->device = LEGACY_GAMEPAD;
    snprintf(player->id, sizeof player->id, "%s", value + 8);
  } else
    return 0;
  return 1;
}

int parse_owner(const char *value, int8_t *owner) {
  unsigned player;
  if (strcmp(value, "unassigned") == 0) {
    *owner = kSettingsUnassigned;
    return 1;
  }
  if (!number(value, 0, kSettingsPlayers - 1, &player))
    return 0;
  *owner = (int8_t)player;
  return 1;
}

int parse_profile_binding(Settings *settings, const char *key,
                          const char *value) {
  unsigned profile, row, code;
  char tail;
  int n = sscanf(key, "input.profile%u.row%u%c", &profile, &row, &tail);
  if (n != 2 || profile >= kSettingsKeyboardProfiles || row >= kSettingsRows)
    return 0;
  if (!number(value, 0, 65535, &code))
    return 0;
  settings->keyboard_profile[profile].keyboard[row] = (uint16_t)code;
  settings->keyboard_profile[profile].keyboard_set[row] = 1;
  return 1;
}

int parse_line(ParseState *state, char *line) {
  Settings *settings = &state->settings;
  char *eq = strchr(line, '=');
  char *key, *value;
  unsigned player, n, profile, slot;
  char setting_key[48];

  if (!eq)
    return 0;
  *eq = 0;
  key = trim(line);
  value = trim(eq + 1);
  if (strncmp(key, "ui.hud.", 7) == 0)
    return x2_hud_settings_parse(&settings->hud, key + 7, value);
  if (strcmp(key, "video.width") == 0)
    return number(value, X2_MIN_WIDTH, X2_MAX_WIDTH, &settings->width);
  if (strcmp(key, "video.height") == 0)
    return number(value, X2_MIN_HEIGHT, X2_MAX_HEIGHT, &settings->height);
  if (strcmp(key, "video.mode") == 0)
    return window_mode_parse(value, &settings->window_mode);
  if (strcmp(key, "video.dynamic_shadows") == 0) {
    unsigned enabled;
    if (!number(value, 0, 1, &enabled))
      return 0;
    settings->dynamic_shadows = (uint8_t)enabled;
    return 1;
  }
  if (strcmp(key, "video.shadow_resolution") == 0) {
    unsigned resolution;
    if (!number(value, 512, 4096, &resolution) ||
        (resolution != 512 && resolution != 1024 && resolution != 2048 &&
         resolution != 4096))
      return 0;
    settings->shadow_resolution = (uint16_t)resolution;
    return 1;
  }
  if (strcmp(key, "ui.text_scale") == 0) {
    /* 0 is AUTO and the only value below 0.5; above 4 the text stops
       fitting any panel the game has. Both ends REFUSE rather than
       clamping, so a typo is reported instead of silently applied. */
    char *end;
    double parsed = strtod(value, &end);
    if (end == value || *end)
      return 0;
    if (parsed != 0.0 && (parsed < 0.5 || parsed > 4.0))
      return 0;
    settings->text_scale = (float)parsed;
    return 1;
  }
  if (strcmp(key, "boot.mode") == 0)
    return boot_mode_parse(value, &settings->boot_mode);
  if (strcmp(key, "gameplay.extraction_revive") == 0)
    return extraction_revive_parse(value, &settings->extraction_revive);
  if (strcmp(key, "input.touch_controls") == 0) {
    unsigned mode;
    if (!number(value, kTouchControlsOff, kTouchControlsAlways, &mode))
      return 0;
    settings->touch_controls = (uint8_t)mode;
    return 1;
  }
  if (strcmp(key, "input.assignment_version") == 0) {
    state->saw_grid = 1;
    return strcmp(value, "2") == 0;
  }
  for (profile = 0; profile < kSettingsKeyboardProfiles; profile++) {
    snprintf(setting_key, sizeof setting_key, "input.keyboard%u.player",
             profile);
    if (strcmp(key, setting_key) == 0) {
      state->saw_grid = 1;
      return parse_owner(value, &settings->keyboard_player[profile]);
    }
  }
  for (slot = 0; slot < kSettingsControllerAssignments; slot++) {
    snprintf(setting_key, sizeof setting_key, "input.controller%u.id", slot);
    if (strcmp(key, setting_key) == 0) {
      if (strlen(value) >= kSettingsDeviceId)
        return 0;
      state->saw_grid = 1;
      snprintf(settings->controller[slot].id,
               sizeof settings->controller[slot].id, "%s", value);
      return 1;
    }
    snprintf(setting_key, sizeof setting_key, "input.controller%u.player",
             slot);
    if (strcmp(key, setting_key) == 0) {
      state->saw_grid = 1;
      return parse_owner(value, &settings->controller[slot].player);
    }
  }
  for (player = 0; player < kSettingsPlayers; player++) {
    snprintf(setting_key, sizeof setting_key, "input.player%u.device", player);
    if (strcmp(key, setting_key) == 0) {
      state->saw_legacy = 1;
      return parse_legacy_device(&state->legacy_player[player], value);
    }
    snprintf(setting_key, sizeof setting_key, "input.player%u.profile", player);
    if (strcmp(key, setting_key) == 0) {
      if (!number(value, 0, kSettingsKeyboardProfiles - 1, &profile))
        return 0;
      state->saw_legacy = 1;
      state->legacy_player[player].profile = profile;
      return 1;
    }
  }
  n = (unsigned)strlen("input.profile");
  if (strncmp(key, "input.profile", n) == 0)
    return parse_profile_binding(settings, key, value);
  return 0;
}

int settings_valid(const Settings *settings) {
  unsigned i, j;
  if (!x2_hud_settings_valid(&settings->hud))
    return 0;
  if ((unsigned)settings->boot_mode > (unsigned)BootMode::Continue ||
      settings->touch_controls > kTouchControlsAlways ||
      (unsigned)settings->extraction_revive > (unsigned)ExtractionRevive::Paid)
    return 0;
  if (settings->dynamic_shadows > 1 || (settings->shadow_resolution != 512 &&
                                        settings->shadow_resolution != 1024 &&
                                        settings->shadow_resolution != 2048 &&
                                        settings->shadow_resolution != 4096))
    return 0;
  for (i = 0; i < kSettingsKeyboardProfiles; i++) {
    int owner = settings->keyboard_player[i];
    if (!input_owner_valid(owner))
      return 0;
    for (j = i + 1; owner >= 0 && j < kSettingsKeyboardProfiles; j++)
      if (settings->keyboard_player[j] == owner)
        return 0;
  }
  for (i = 0; i < kSettingsControllerAssignments; i++) {
    const ControllerAssignment *a = &settings->controller[i];
    if (!input_owner_valid(a->player) || (a->player >= 0 && !a->id[0]))
      return 0;
    if (!a->id[0])
      continue;
    for (j = i + 1; j < kSettingsControllerAssignments; j++)
      if (strcmp(a->id, settings->controller[j].id) == 0 ||
          (a->player >= 0 && a->player == settings->controller[j].player))
        return 0;
  }
  return input_assignments_valid(settings);
}

int migrate_legacy(ParseState *state) {
  KeyboardProfile original[kSettingsKeyboardProfiles];
  unsigned char reserved[kSettingsKeyboardProfiles] = {0};
  unsigned i, profile;
  Settings *settings = &state->settings;
  if (!state->saw_legacy)
    return 1;
  if (state->saw_grid)
    return 0;
  memcpy(original, settings->keyboard_profile, sizeof original);
  for (i = 0; i < kSettingsPlayers; i++) {
    LegacyPlayer *legacy = &state->legacy_player[i];
    if (legacy->device == LEGACY_AUTO || legacy->device == LEGACY_KEYBOARD)
      reserved[legacy->profile] = 1;
  }
  for (i = 0; i < kSettingsKeyboardProfiles; i++)
    settings->keyboard_player[i] = kSettingsUnassigned;
  memset(settings->controller, 0, sizeof settings->controller);
  for (i = 0; i < kSettingsControllerAssignments; i++)
    settings->controller[i].player = kSettingsUnassigned;
  for (i = 0; i < kSettingsPlayers; i++) {
    LegacyPlayer *legacy = &state->legacy_player[i];
    if (legacy->device == LEGACY_AUTO || legacy->device == LEGACY_KEYBOARD) {
      profile = legacy->profile;
      if (settings->keyboard_player[profile] != kSettingsUnassigned) {
        for (profile = 0; profile < kSettingsKeyboardProfiles; profile++)
          if (!reserved[profile] &&
              settings->keyboard_player[profile] == kSettingsUnassigned)
            break;
        if (profile == kSettingsKeyboardProfiles)
          return 0;
        settings->keyboard_profile[profile] = original[legacy->profile];
      }
      if (!settings_assign_keyboard(settings, profile, (int)i))
        return 0;
    }
    if (legacy->device == LEGACY_GAMEPAD &&
        !settings_assign_controller(settings, legacy->id, (int)i))
      return 0;
  }
  return 1;
}
} // namespace

int settings_load(Settings *settings, const char *path, char *why, int whyn) {
  ParseState parsed;
  FILE *file;
  char line[512];
  unsigned lineno = 0;

  unsigned player;
  memset(&parsed, 0, sizeof parsed);
  settings_defaults(&parsed.settings);
  parsed.legacy_player[0].device = LEGACY_AUTO;
  for (player = 0; player < kSettingsPlayers; player++)
    parsed.legacy_player[player].profile = player;
  file = fopen(path, "r");
  if (!file) {
    if (errno == ENOENT) {
      *settings = parsed.settings;
      reason(why, whyn, "settings file does not exist; defaults loaded");
      return 1;
    }
    if (why)
      snprintf(why, (size_t)whyn, "cannot open %s: %s", path, strerror(errno));
    return 0;
  }
  while (fgets(line, sizeof line, file)) {
    char *text;
    lineno++;
    if (!strchr(line, '\n') && !feof(file)) {
      if (why)
        snprintf(why, (size_t)whyn, "%s:%u is longer than %zu bytes", path,
                 lineno, sizeof line - 2u);
      fclose(file);
      return 0;
    }
    text = trim(line);
    if (!*text || *text == '#')
      continue;
    if (!parse_line(&parsed, text)) {
      if (why)
        snprintf(why, (size_t)whyn, "%s:%u has an unknown key or invalid value",
                 path, lineno);
      fclose(file);
      return 0;
    }
  }
  if (ferror(file)) {
    if (why)
      snprintf(why, (size_t)whyn, "cannot read %s: %s", path, strerror(errno));
    fclose(file);
    return 0;
  }
  fclose(file);
  if (!migrate_legacy(&parsed) || !settings_valid(&parsed.settings)) {
    if (why)
      snprintf(why, (size_t)whyn,
               "%s has invalid settings or conflicting device assignments",
               path);
    return 0;
  }
  *settings = parsed.settings;
  reason(why, whyn, "settings loaded");
  return 1;
}

int settings_save(const Settings *settings, const char *path, char *why,
                  int whyn) {
  char pending[1200];
  FILE *file;
  unsigned slot, profile, row;

  if (!settings || !settings_valid(settings)) {
    reason(why, whyn,
           "settings contain invalid values or conflicting device assignments");
    return 0;
  }
  if (snprintf(pending, sizeof pending, "%s.new", path) >=
      (int)sizeof pending) {
    reason(why, whyn, "settings path is too long");
    return 0;
  }
  file = fopen(pending, "w");
  if (!file) {
    if (why)
      snprintf(why, (size_t)whyn, "cannot write %s: %s", pending,
               strerror(errno));
    return 0;
  }
  fprintf(file, "# x2native settings -- edited by the in-game RmlUi menu\n");
  fprintf(file, "video.width=%u\nvideo.height=%u\nvideo.mode=%s\n",
          settings->width, settings->height,
          window_mode_name(settings->window_mode));
  fprintf(file, "video.dynamic_shadows=%u\nvideo.shadow_resolution=%u\n",
          settings->dynamic_shadows, settings->shadow_resolution);
  fprintf(file, "ui.text_scale=%g\n", (double)settings->text_scale);
  if (!x2_hud_settings_write(&settings->hud, file)) {
    reason(why, whyn, "cannot write HUD settings");
    fclose(file);
    return 0;
  }
  fprintf(file, "boot.mode=%s\n", boot_mode_name(settings->boot_mode));
  fprintf(file, "gameplay.extraction_revive=%s\n",
          extraction_revive_name(settings->extraction_revive));
  fprintf(file, "input.touch_controls=%u\n", settings->touch_controls);
  fprintf(file, "input.assignment_version=2\n");
  for (profile = 0; profile < kSettingsKeyboardProfiles; profile++) {
    int owner = settings->keyboard_player[profile];
    fprintf(file, "input.keyboard%u.player=", profile);
    if (owner < 0)
      fprintf(file, "unassigned\n");
    else
      fprintf(file, "%d\n", owner);
  }
  for (slot = 0; slot < kSettingsControllerAssignments; slot++) {
    const ControllerAssignment *assignment = &settings->controller[slot];
    fprintf(file, "input.controller%u.id=%s\n", slot, assignment->id);
    fprintf(file, "input.controller%u.player=", slot);
    if (assignment->player < 0)
      fprintf(file, "unassigned\n");
    else
      fprintf(file, "%d\n", assignment->player);
  }
  for (profile = 0; profile < kSettingsKeyboardProfiles; profile++) {
    const KeyboardProfile *p = &settings->keyboard_profile[profile];
    for (row = 0; row < kSettingsRows; row++) {
      if (p->keyboard_set[row])
        fprintf(file, "input.profile%u.row%u=%u\n", profile, row,
                p->keyboard[row]);
    }
  }
  if (fclose(file) != 0 || x2_replace_file(pending, path) != 0) {
    if (why)
      snprintf(why, (size_t)whyn, "cannot publish %s: %s", path,
               strerror(errno));
    return 0;
  }
  reason(why, whyn, "settings saved");
  return 1;
}

} // namespace x2::config
