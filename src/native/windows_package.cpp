/* What the portable Windows folder tells the port about itself: a double-click
 * supplies no arguments and no environment, and the compile-time UI path names
 * the build tree, so the executable finds its own `ui` folder. */
#include "windows_package.h"

#include "../config/environment.h"
#include "x2_log.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace x2::native {

namespace {

constexpr std::size_t kPathCapacity = 4096;

bool is_directory(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR;
}

bool is_file(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFREG;
}

/* Length of the directory part of `path`, accepting either separator. */
std::size_t directory_length(const char *path) {
  const char *slash = std::strrchr(path, '/');
  const char *back = std::strrchr(path, '\\');
  if (back && (!slash || back > slash)) {
    slash = back;
  }
  return slash ? static_cast<std::size_t>(slash - path) : 0;
}

} // namespace

int windows_package_init_from(const char *executable) {
  char path[kPathCapacity];
  if (!executable) {
    return 0;
  }
  const std::size_t length = directory_length(executable);
  if (length == 0 || length + 32 >= sizeof path) {
    return 0;
  }
  std::snprintf(path, sizeof path, "%.*s/%s", static_cast<int>(length),
                executable, kWindowsPackageMarker);
  if (!is_file(path)) {
    return 0;
  }
  std::snprintf(path, sizeof path, "%.*s/ui", static_cast<int>(length),
                executable);
  if (!is_directory(path)) {
    x2_log_error("windows package: the UI resources are MISSING from this "
                 "package (%s); the settings overlay will not draw.\n",
                 path);
  } else {
    config_override_set(x2::config::ConfigOverride::UiResourceDir, path, 0);
  }
  x2_log_error("windows package: running as the packaged product from %.*s\n",
               static_cast<int>(length), executable);
  return 1;
}

int windows_package_init() {
#if defined(_WIN32)
  char path[kPathCapacity];
  const DWORD length = GetModuleFileNameA(nullptr, path, sizeof path);
  if (length == 0 || length >= sizeof path) {
    return 0;
  }
  return windows_package_init_from(path);
#else
  return 0;
#endif
}

} // namespace x2::native
