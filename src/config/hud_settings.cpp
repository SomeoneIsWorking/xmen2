#include "hud_settings.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static int scale_valid(unsigned value) {
  return value >= X2_HUD_SCALE_MIN && value <= X2_HUD_SCALE_MAX;
}

void x2_hud_settings_defaults(X2HudSettings *settings) {
  *settings = (X2HudSettings){X2_HUD_LAYOUT_AUTO, 100, 100, 100, 2};
}

int x2_hud_settings_valid(const X2HudSettings *settings) {
  return settings && (unsigned)settings->layout <= X2_HUD_LAYOUT_MOBILE &&
         scale_valid(settings->vitals_scale_percent) &&
         scale_valid(settings->potions_scale_percent) &&
         scale_valid(settings->portraits_scale_percent) &&
         settings->safe_inset_percent <= X2_HUD_INSET_MAX;
}

const char *x2_hud_layout_name(X2HudLayout layout) {
  static const char *const names[] = {"auto", "retail", "mobile"};
  return (unsigned)layout <= X2_HUD_LAYOUT_MOBILE ? names[layout] : "invalid";
}

const char *x2_hud_layout_label(X2HudLayout layout) {
  static const char *const labels[] = {"Automatic", "Original", "Mobile"};
  return (unsigned)layout <= X2_HUD_LAYOUT_MOBILE ? labels[layout] : "Invalid";
}

static int parse_percent(const char *value, unsigned min, unsigned max,
                         unsigned *out) {
  char *end;
  errno = 0;
  unsigned long parsed = strtoul(value, &end, 10);
  if (errno || end == value || *end || parsed < min || parsed > max)
    return 0;
  *out = (unsigned)parsed;
  return 1;
}

int x2_hud_settings_parse(X2HudSettings *settings, const char *key,
                          const char *value) {
  if (strcmp(key, "layout") == 0) {
    for (unsigned i = X2_HUD_LAYOUT_AUTO; i <= X2_HUD_LAYOUT_MOBILE; i++) {
      if (strcmp(value, x2_hud_layout_name((X2HudLayout)i)) == 0) {
        settings->layout = (X2HudLayout)i;
        return 1;
      }
    }
    return 0;
  }
  if (strcmp(key, "vitals_scale_percent") == 0)
    return parse_percent(value, X2_HUD_SCALE_MIN, X2_HUD_SCALE_MAX,
                         &settings->vitals_scale_percent);
  if (strcmp(key, "potions_scale_percent") == 0)
    return parse_percent(value, X2_HUD_SCALE_MIN, X2_HUD_SCALE_MAX,
                         &settings->potions_scale_percent);
  if (strcmp(key, "portraits_scale_percent") == 0)
    return parse_percent(value, X2_HUD_SCALE_MIN, X2_HUD_SCALE_MAX,
                         &settings->portraits_scale_percent);
  if (strcmp(key, "safe_inset_percent") == 0)
    return parse_percent(value, 0, X2_HUD_INSET_MAX,
                         &settings->safe_inset_percent);
  return 0;
}

int x2_hud_settings_write(const X2HudSettings *settings, FILE *file) {
  if (!x2_hud_settings_valid(settings))
    return 0;
  return fprintf(file,
                 "ui.hud.layout=%s\nui.hud.vitals_scale_percent=%u\n"
                 "ui.hud.potions_scale_percent=%u\n"
                 "ui.hud.portraits_scale_percent=%u\n"
                 "ui.hud.safe_inset_percent=%u\n",
                 x2_hud_layout_name(settings->layout),
                 settings->vitals_scale_percent,
                 settings->potions_scale_percent,
                 settings->portraits_scale_percent,
                 settings->safe_inset_percent) >= 0;
}
