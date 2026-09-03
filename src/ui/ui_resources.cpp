#include "ui_resources.h"
#include "../config/config_directory.h"

#include <cstdio>
#include <cstdlib>

#ifndef X2_UI_RESOURCE_DIR
#define X2_UI_RESOURCE_DIR "."
#endif

extern "C" const char *x2_ui_resource_path(const char *name) {
  static char path[4096];
#if defined(__ANDROID__)
  const char *directory = x2_config_directory();
  if (!directory || !directory[0])
    return "";
  std::snprintf(path, sizeof path, "%s/ui/%s", directory, name ? name : "");
  return path;
#else
  const char *directory = std::getenv("X2_UI_RESOURCE_DIR");
  if (!directory || !directory[0])
    directory = X2_UI_RESOURCE_DIR;
  std::snprintf(path, sizeof path, "%s/%s", directory, name ? name : "");
  return path;
#endif
}
