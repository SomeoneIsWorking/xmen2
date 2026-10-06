#include "../config/environment.h"
#include "../native/install_archive.h"
#include "../native/install_picker.h"
#include "browser_log.hpp"
#include "web_request.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include <emscripten/emscripten.h>
#include <lucent/platform_c.h>
#include <web_port/storage.h>

extern "C" int x2native_main(int argc, char **argv);

namespace {
constexpr char ready_path[] = "/opfs/install.ready";
constexpr char ready_temporary[] = "/opfs/install.ready.tmp";
constexpr char ready_token[] = "x2-install-ready-v1\n";

bool install_ready() {
  FILE *file = std::fopen(ready_path, "rb");
  if (!file) {
    return false;
  }
  char token[sizeof(ready_token)]{};
  const bool ready =
      std::fread(token, 1, sizeof(ready_token) - 1, file) ==
          sizeof(ready_token) - 1 &&
      std::memcmp(token, ready_token, sizeof(ready_token) - 1) == 0 &&
      std::fgetc(file) == EOF && !std::ferror(file);
  return std::fclose(file) == 0 && ready;
}

bool mark_install_ready() {
  FILE *file = std::fopen(ready_temporary, "wb");
  if (!file) {
    return false;
  }
  const bool written = std::fwrite(ready_token, 1, sizeof(ready_token) - 1,
                                   file) == sizeof(ready_token) - 1;
  if (std::fclose(file) != 0 || !written) {
    return false;
  }
  return std::rename(ready_temporary, ready_path) == 0;
}
} // namespace

static void report_setup(const char *message, int failed) {
  MAIN_THREAD_EM_ASM(
      {
        if (Module['onSetupStatus'])
          Module['onSetupStatus'](UTF8ToString($0), Boolean($1));
      },
      message, failed);
}

static void report_unpack_progress(std::uint64_t done, std::uint64_t total,
                                   void *) {
  const int percent = total ? static_cast<int>(done * 100 / total) : 0;
  /* Hand the page the same expanded-byte counts the extractor reported, so the
   * bar shows the work actually done and not only a derived percentage. */
  MAIN_THREAD_EM_ASM(
      {
        if (Module['onUnpackProgress'])
          Module['onUnpackProgress']($0, $1, $2);
      },
      percent, static_cast<double>(done), static_cast<double>(total));
}

static int run_application(int argc, char **argv) {
  /* The routing rule and the flag names live in web_request.cpp, where the
   * `?arg=`-append behaviour that produced issue #151 is unit-tested. */
  const x2::web::launch_request request =
      x2::web::classify_launch_request(argc, argv);
  if (request == x2::web::launch_request::conflict) {
    report_setup(
        "The page asked for a ZIP import and the gameplay test together; "
        "they select different routes.",
        1);
    return 1;
  }
  const bool importing = request == x2::web::launch_request::import_archive;
  const bool gameplay_test = request == x2::web::launch_request::gameplay_test;
  report_setup(importing ? "Checking and unpacking your game files..."
                         : "Checking your saved installation...",
               0);
  char directory[4096];
  char reason[1024]{};
  const char *selection =
      importing ? "/opfs/incoming/input.zip" : "/opfs/install";
  char executable[4096];
  const bool selected =
      importing
          ? x2_install_archive_extract_unpublished(
                selection, "/opfs/install", executable, sizeof(executable),
                reason, sizeof(reason), report_unpack_progress, nullptr) &&
                x2_install_picker_directory_from_executable(
                    executable, directory, sizeof(directory))
          : install_ready() && x2_install_picker_resolve_selection(
                                   selection, nullptr, directory,
                                   sizeof(directory), reason, sizeof(reason));
  if (!selected) {
    if (!reason[0]) {
      std::snprintf(
          reason, sizeof(reason), "%s",
          importing ? "That ZIP did not produce a usable installation."
                    : "The saved installation is incomplete or unavailable.");
    }
    report_setup(reason, 1);
    return 1;
  }
  if (importing && !mark_install_ready()) {
    report_setup("The game files passed validation, but their completion "
                 "marker could not be saved.",
                 1);
    return 1;
  }
  if (importing && std::remove(selection) != 0) {
    report_setup(
        "Game files are ready, but the imported ZIP could not be removed.", 1);
    return 1;
  }
  if (x2_config_override_set(kX2ConfigGamePcDir, directory, 1) != 0 ||
      x2_config_override_set(kX2ConfigUiResourceDir, "/ui", 1) != 0) {
    report_setup(
        "The validated installation could not be published to the game.", 1);
    return 1;
  }
  if (gameplay_test &&
      x2_config_override_set(kX2ConfigBootMap,
                             x2::web::gameplay_test_map(argc, argv), 1) != 0) {
    report_setup("The gameplay test map could not be selected.", 1);
    return 1;
  }
  report_setup("Starting X-Men Legends II...", 0);
  MAIN_THREAD_EM_ASM({
    if (Module['onGameReady'])
      Module['onGameReady']();
  });
  char program[] = "x2native";
  char packaged[] = "--appimage";
  std::vector<char *> native_args{program, packaged, directory};
  for (int i = 1; i < argc; i++) {
    if (!x2::web::is_entry_request(argv[i])) {
      native_args.push_back(argv[i]);
    }
  }
  native_args.push_back(nullptr);
  return x2native_main(static_cast<int>(native_args.size()) - 1,
                       native_args.data());
}

int main(int argc, char **argv) {
  x2::web::install_browser_log_sink();
  if (!web_port_mount_storage("/opfs")) {
    report_setup("The browser could not open private game storage.", 1);
    return 1;
  }
  int result = 1;
  if (lucent_platform_set_user_data_directory("/opfs/user")) {
    result = run_application(argc, argv);
  } else {
    report_setup("The browser could not select private game storage.", 1);
  }
  /* The sink was holding lines back to keep the page's console off the game
     thread's critical path; this is the last moment before the runtime goes
     away and nothing else can hand them over. */
  x2::web::flush_browser_log();
  if (!web_port_unmount_storage("/opfs")) {
    report_setup("Private game storage could not be closed safely.", 1);
    result = 1;
  }
  return result;
}
