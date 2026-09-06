#ifndef X2_HUD_SETTINGS_H
#define X2_HUD_SETTINGS_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  X2_HUD_LAYOUT_AUTO,
  X2_HUD_LAYOUT_RETAIL,
  X2_HUD_LAYOUT_MOBILE
} X2HudLayout;

enum { X2_HUD_SCALE_MIN = 50, X2_HUD_SCALE_MAX = 150, X2_HUD_INSET_MAX = 10 };

typedef struct {
  X2HudLayout layout;
  unsigned vitals_scale_percent;
  unsigned potions_scale_percent;
  unsigned portraits_scale_percent;
  unsigned safe_inset_percent;
} X2HudSettings;

void x2_hud_settings_defaults(X2HudSettings *settings);
int x2_hud_settings_valid(const X2HudSettings *settings);
const char *x2_hud_layout_name(X2HudLayout layout);
const char *x2_hud_layout_label(X2HudLayout layout);
/* Parses a suffix after ui.hud.; refuses unknown keys and leaves settings
   unchanged on invalid input. Percentages are whole numbers. */
int x2_hud_settings_parse(X2HudSettings *settings, const char *key,
                          const char *value);
int x2_hud_settings_write(const X2HudSettings *settings, FILE *file);

#ifdef __cplusplus
}
#endif

#endif
