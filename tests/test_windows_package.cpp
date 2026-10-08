/* A wrong answer silently turns the portable folder into a developer run
   (project .env, no picker) or a build tree into a package. */
#include "../src/config/environment.h"
#include "windows_package.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#define MAKE_DIR(path) mkdir(path, 0755)
#endif

static int failures;

static void check(bool ok, const char *what) {
  if (!ok) {
    std::printf("FAIL: %s\n", what);
    failures++;
  }
}

static void touch(const std::string &path) {
  FILE *file = std::fopen(path.c_str(), "wb");
  if (file) {
    std::fclose(file);
  }
}

int main(int, char **argv) {
  const std::string root = std::string(argv[0]) + ".scratch";
  MAKE_DIR(root.c_str());
  const std::string exe = root + "/X-Men Legends II.exe";
  const std::string marker = root + "/" + x2::native::kWindowsPackageMarker;
  std::remove(marker.c_str());

  check(x2::native::windows_package_init_from(exe.c_str()) == 0,
        "no marker is a developer launch");
  const char *none = x2_config_override_get(kX2ConfigUiResourceDir);
  check(!none || !none[0], "a developer launch publishes nothing");

  touch(marker);
  MAKE_DIR((root + "/ui").c_str());
  check(x2::native::windows_package_init_from(exe.c_str()) == 1,
        "the marker makes it the package");
  const char *published = x2_config_override_get(kX2ConfigUiResourceDir);
  check(published && std::string(published) == root + "/ui",
        "the ui folder beside the exe is published");

  x2_config_override_set(kX2ConfigUiResourceDir, "/elsewhere", 1);
  x2::native::windows_package_init_from(exe.c_str());
  published = x2_config_override_get(kX2ConfigUiResourceDir);
  check(published && std::strcmp(published, "/elsewhere") == 0,
        "an existing choice is not overwritten");

  check(x2::native::windows_package_init_from("X-Men Legends II.exe") == 0,
        "a bare name has no directory");
  check(x2::native::windows_package_init_from(nullptr) == 0, "null is refused");

  std::remove(marker.c_str());
  std::printf("test_windows_package: %s\n", failures ? "FAILED" : "passed");
  return failures ? 1 : 0;
}
