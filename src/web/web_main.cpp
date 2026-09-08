#include "../config/environment.h"
#include "../native/install_picker.h"

#include <cstdio>
#include <cstring>

#include <emscripten/emscripten.h>
#include <lucent/platform_c.h>
#include <lucent/web.h>

extern "C" int x2native_main(int argc, char **argv);

static void report_setup(const char *message, int failed) {
  MAIN_THREAD_EM_ASM(
      {
        if (Module['onSetupStatus'])
          Module['onSetupStatus'](UTF8ToString($0), Boolean($1));
      },
      message, failed);
}

static int run_application(int argc, char **argv) {
  const bool importing = argc == 2 && std::strcmp(argv[1], "--import") == 0;
  if (argc > 1 && !importing) {
    report_setup("Unrecognized browser launch request.", 1);
    return 1;
  }
  report_setup(importing ? "Checking and unpacking your game files..."
                         : "Checking your saved installation...",
               0);
  char directory[4096];
  char reason[1024];
  const char *selection =
      importing ? "/opfs/incoming/input.zip" : "/opfs/install";
  if (!x2_install_picker_resolve_selection(selection, "/opfs/install",
                                           directory, sizeof(directory), reason,
                                           sizeof(reason))) {
    report_setup(reason, 1);
    return 1;
  }
  if (importing && std::remove(selection) != 0) {
    report_setup(
        "Game files are ready, but the imported ZIP could not be removed.", 1);
    return 1;
  }
  if (!x2_config_override_set(kX2ConfigGamePcDir, directory, 1) ||
      !x2_config_override_set(kX2ConfigUiResourceDir, "/ui", 1)) {
    report_setup(
        "The validated installation could not be published to the game.", 1);
    return 1;
  }
  report_setup("Starting X-Men Legends II...", 0);
  MAIN_THREAD_EM_ASM({
    if (Module['onGameReady'])
      Module['onGameReady']();
  });
  char program[] = "x2native";
  char packaged[] = "--appimage";
  char *native_args[] = {program, packaged, directory, nullptr};
  return x2native_main(3, native_args);
}

int main(int argc, char **argv) {
  if (!lucent_web_mount_storage("/opfs")) {
    report_setup("The browser could not open private game storage.", 1);
    return 1;
  }
  int result = 1;
  if (lucent_platform_set_user_data_directory("/opfs/user"))
    result = run_application(argc, argv);
  else
    report_setup("The browser could not select private game storage.", 1);
  if (!lucent_web_unmount_storage("/opfs")) {
    report_setup("Private game storage could not be closed safely.", 1);
    result = 1;
  }
  return result;
}
