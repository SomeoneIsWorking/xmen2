/*
 * What a macOS .app has to tell the port about itself.
 *
 * A double-clicked bundle arrives with no arguments, no terminal, no
 * X2_UI_RESOURCE_DIR and no Homebrew on its library path -- the launch shape
 * an AppImage solves with a shell AppRun. A bundle has no equivalent place to
 * put one: its CFBundleExecutable is the process the user sees in the Dock,
 * and wrapping it in a script makes the game a child of the thing macOS is
 * tracking. So the executable answers for itself here: it looks at the path it
 * was launched from, and if that path is inside `.app/Contents/MacOS`, it is
 * the packaged product and it publishes its own bundle's resources.
 *
 * Everything published is published WITHOUT overwrite. A developer running the
 * binary out of a bundle with X2_UI_RESOURCE_DIR already exported still gets
 * the directory they asked for; the bundle only fills in what nothing else
 * has decided.
 *
 * The renderer needs one thing that has no Apple equivalent: SDL_GPU is
 * driven from SPIR-V here, so on macOS it runs the Vulkan backend over
 * MoltenVK. Neither the loader nor the driver is part of the OS, so both ship
 * inside the bundle, and SDL is pointed straight at the bundled loader rather
 * than left to search a machine that may have none.
 */
#include "macos_bundle.h"

#include "../config/environment.h"
#include "x2_log.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#if defined(__APPLE__)
#include <SDL3/SDL.h>
#endif

#define BUNDLE_PATH 4096

/* `.../Foo.app/Contents/MacOS/foo` -> `.../Foo.app`, or 0 when the executable
   is not inside a bundle at all. Purely textual so it can be tested without a
   bundle on disk; the caller checks what it found before publishing. */
int x2_macos_bundle_root(const char *executable, char *root, unsigned capacity) {
  static const char k_suffix[] = "/Contents/MacOS/";
  const char *at;
  const char *found = NULL;
  size_t length;

  if (!executable || !root || capacity < 2u)
    return 0;
  /* The LAST occurrence: a bundle inside a path that itself contains the
     sequence still resolves to the innermost bundle, which is the one this
     executable belongs to. */
  for (at = executable; (at = strstr(at, k_suffix)) != NULL; at++)
    found = at;
  if (!found || found == executable)
    return 0;
  /* Nothing may follow the executable's own name: a path with another slash
     after MacOS/ is not this bundle's main executable. */
  if (strchr(found + sizeof k_suffix - 1u, '/'))
    return 0;
  length = (size_t)(found - executable);
  if (length < 5u || length >= capacity)
    return 0;
  if (memcmp(executable + length - 4u, ".app", 4u) != 0)
    return 0;
  memcpy(root, executable, length);
  root[length] = 0;
  return 1;
}

#if defined(__APPLE__)
static int directory_exists(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static int file_exists(const char *path) {
  struct stat info;
  return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

/* A missing piece is REPORTED, never quietly skipped: a bundle that lost its
   Vulkan driver in packaging otherwise looks like a machine without a GPU. */
static void publish_file(const char *path, X2ConfigOverride variable,
                         const char *what) {
  const char *configured = x2_config_override_get(variable);
  if (!file_exists(path)) {
    x2_log_error("macos bundle: %s is MISSING from this bundle (%s); the "
                 "packaged app is incomplete.\n",
                 what, path);
    return;
  }
  if (configured && configured[0])
    return; /* the machine already chose one; the bundle does not argue */
  x2_config_override_set(variable, path, 0);
}
#endif

int x2_macos_bundle_init(const char *executable) {
#if defined(__APPLE__)
  char root[BUNDLE_PATH];
  char path[BUNDLE_PATH];

  if (!x2_macos_bundle_root(executable, root, sizeof root))
    return 0;

  snprintf(path, sizeof path, "%s/Contents/Resources/ui", root);
  if (directory_exists(path))
    x2_config_override_set(kX2ConfigUiResourceDir, path, 0);
  else
    x2_log_error("macos bundle: the UI resources are MISSING from this "
                 "bundle (%s); the settings overlay will not draw.\n",
                 path);

  /* VK_DRIVER_FILES is the current name and VK_ICD_FILENAMES the one older
     loaders read; a bundle cannot know which loader a machine will end up
     using, and the two never disagree because both are set from one file. */
  snprintf(path, sizeof path,
           "%s/Contents/Resources/vulkan/icd.d/MoltenVK_icd.json", root);
  publish_file(path, kX2ConfigVulkanDriverFiles, "the MoltenVK driver manifest");
  publish_file(path, kX2ConfigVulkanIcdFilenames,
               "the MoltenVK driver manifest");

  snprintf(path, sizeof path, "%s/Contents/Frameworks/libvulkan.1.dylib", root);
  if (file_exists(path))
    SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, path);
  else
    x2_log_error("macos bundle: the Vulkan loader is MISSING from this "
                 "bundle (%s); SDL will search this machine for one.\n",
                 path);

  x2_log_error("macos bundle: running as the packaged product from %s\n", root);
  return 1;
#else
  (void)executable;
  return 0;
#endif
}
