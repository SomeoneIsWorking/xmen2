#include "ui_resources.h"
#include "../config/config_directory.h"
#include "../config/environment.h"

#include <cstdio>

namespace x2::ui {

#ifndef X2_UI_RESOURCE_DIR
#define X2_UI_RESOURCE_DIR "."
#endif

const char *ui_resource_path(const char *name) {
  static char path[4096];
#if defined(__ANDROID__)
  const char *directory = x2::config::config_directory();
  if (!directory || !directory[0])
    return "";
  std::snprintf(path, sizeof path, "%s/ui/%s", directory, name ? name : "");
  return path;
#else
  const char *directory =
      config_override_get(x2::config::ConfigOverride::UiResourceDir);
  if (!directory || !directory[0])
    directory = X2_UI_RESOURCE_DIR;
  std::snprintf(path, sizeof path, "%s/%s", directory, name ? name : "");
  return path;
#endif
}

} // namespace x2::ui
