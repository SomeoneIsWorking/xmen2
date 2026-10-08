#include "settings_store.h"
#include "../native/x2_log.h"

#include "shell32.h"

#include <stdio.h>

namespace x2::config {

namespace {
Settings g_settings;
char g_path[1200];
int g_ready;
} // namespace

void settings_store_init(void) {
  const char *dir;
  char why[256];

  if (g_ready)
    return;
  g_ready = 1;
  settings_defaults(&g_settings);
  dir = x2::native::save_dir();
  if (!dir || !dir[0]) {
    x2_log_error("SETTINGS: save directory is unavailable; defaults "
                 "are active but changes cannot be persisted.\n");
    return;
  }
  if (snprintf(g_path, sizeof g_path, "%s/x2native.conf", dir) >=
      (int)sizeof g_path) {
    g_path[0] = 0;
    x2_log_error("SETTINGS: save directory path is too long; defaults "
                 "are active but changes cannot be persisted.\n");
    return;
  }
  if (!settings_load(&g_settings, g_path, why, (int)sizeof why)) {
    x2_log_error("SETTINGS: %s. The invalid file was NOT partly "
                 "applied; complete defaults are active.\n",
                 why);
    settings_defaults(&g_settings);
  } else {
    x2_log_error("SETTINGS: %s (%s).\n", why, g_path);
  }
}

Settings *settings_store(void) {
  settings_store_init();
  return &g_settings;
}

int settings_store_save(char *why, int whyn) {
  settings_store_init();
  if (!g_path[0]) {
    if (why && whyn > 0)
      snprintf(why, (size_t)whyn, "settings path is unavailable");
    return 0;
  }
  return settings_save(&g_settings, g_path, why, whyn);
}

const char *settings_store_path(void) {
  settings_store_init();
  return g_path[0] ? g_path : NULL;
}

} // namespace x2::config
