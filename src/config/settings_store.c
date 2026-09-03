#include "settings_store.h"

#include "shell32.h"

#include <stdio.h>

static X2Settings g_settings;
static char g_path[1200];
static int g_ready;

void x2_settings_store_init(void) {
  const char *dir;
  char why[256];

  if (g_ready)
    return;
  g_ready = 1;
  x2_settings_defaults(&g_settings);
  dir = x2_save_dir();
  if (!dir || !dir[0]) {
    fprintf(stderr, "SETTINGS: save directory is unavailable; defaults "
                    "are active but changes cannot be persisted.\n");
    return;
  }
  if (snprintf(g_path, sizeof g_path, "%s/x2native.conf", dir) >=
      (int)sizeof g_path) {
    g_path[0] = 0;
    fprintf(stderr, "SETTINGS: save directory path is too long; defaults "
                    "are active but changes cannot be persisted.\n");
    return;
  }
  if (!x2_settings_load(&g_settings, g_path, why, (int)sizeof why)) {
    fprintf(stderr,
            "SETTINGS: %s. The invalid file was NOT partly "
            "applied; complete defaults are active.\n",
            why);
    x2_settings_defaults(&g_settings);
  } else {
    fprintf(stderr, "SETTINGS: %s (%s).\n", why, g_path);
  }
}

X2Settings *x2_settings_store(void) {
  x2_settings_store_init();
  return &g_settings;
}

int x2_settings_store_save(char *why, int whyn) {
  x2_settings_store_init();
  if (!g_path[0]) {
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn, "settings path is unavailable");
    return 0;
  }
  return x2_settings_save(&g_settings, g_path, why, whyn);
}

const char *x2_settings_store_path(void) {
  x2_settings_store_init();
  return g_path[0] ? g_path : NULL;
}
