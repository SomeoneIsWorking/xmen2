#include "touch_art.h"

#include "win_path.h"

#include <stdio.h>

enum { TOUCH_ART_PATH_MAX = 1024 };

static const char *const kGuestPaths[X2_TOUCH_ART_COUNT] = {
    "textures/ui/talent_icons.igb", "textures/ui/hud.igb"};
static char g_paths[X2_TOUCH_ART_COUNT][TOUCH_ART_PATH_MAX];

const char *x2_touch_art_path(X2TouchArt art) {
  if ((unsigned)art >= X2_TOUCH_ART_COUNT)
    return "";
  if (!g_paths[art][0])
    snprintf(g_paths[art], sizeof g_paths[art], "%s",
             win_path(kGuestPaths[art]));
  return g_paths[art];
}
