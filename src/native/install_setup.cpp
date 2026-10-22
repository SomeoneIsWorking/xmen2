/* The first-run choice of the player's PC installation. On desktop it is the
 * shared setup-ui screen in its own SDL window: Browse opens the OS file
 * dialog, and install_picker judges the executable, folder or ZIP chosen. */
#include "install_setup.h"

#include "../config/environment.h"
#include "android_bridge.h"
#include "install_picker.h"
#include "x2_log.h"

#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include "../ui/ui_resources.h"
#include "setup_ui/setup_ui.h"

#include <SDL3/SDL.h>
#endif

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace x2::native {

namespace {

constexpr unsigned kPathCapacity = 4096;

/* Accept `selection` as the installation; fills `directory` or names why not.
 */
std::string judge(const std::string &selection, std::string &directory) {
  char resolved[kPathCapacity];
  char reason[512];
  if (!install_picker_resolve_selection(selection.c_str(), nullptr, resolved,
                                        sizeof resolved, reason,
                                        sizeof reason)) {
    return reason[0] ? reason
                     : "That is not a usable X-Men Legends II install.";
  }
  directory = resolved;
  return {};
}

#if defined(__ANDROID__)

bool ask(std::string &directory) {
  const char *source = android_install_source();
  if (!source) {
    x2_log_error(
        "install setup: Android setup has not supplied an install source.\n");
    return false;
  }
  const std::string why = judge(source, directory);
  if (!why.empty()) {
    x2_log_error("install setup: Android install source rejected: %s\n",
                 why.c_str());
    return false;
  }
  return true;
}

#elif defined(__EMSCRIPTEN__)

bool ask(std::string &directory) {
  (void)directory;
  x2_log_error("install setup: the browser chooses its install on the page.\n");
  return false;
}

#else

setup_ui::Config screen_config() {
  setup_ui::Config config;
  config.title = "X-Men Legends II";
  config.message = "Choose your PC installation of X-Men Legends II.";
  config.hint = "XMen2.exe, the folder that holds it, or a ZIP containing "
                "exactly one XMen2.exe.";
  config.footer = "Unofficial native port. No game files are included; use "
                  "your own legally obtained copy.";
  config.files = {{"install", "", "PC installation"}};
  config.placement = setup_ui::Placement::Adopt;
  config.browse_label = "Choose installation";
  config.accepted_message = "Installation accepted.";
  return config;
}

bool ask(std::string &directory) {
  setup_ui::Session session(
      screen_config(), setup_ui::SessionOptions{},
      [&directory](const std::vector<setup_ui::StagedFile> &files) {
        directory.clear();
        if (files.size() != 1) {
          return std::string{"Choose one XMen2.exe, folder or ZIP."};
        }
        return judge(files.front().path.string(), directory);
      });
  setup_ui::ViewOptions options;
  options.window_title = "X-Men Legends II setup";
  options.font_path = x2::ui::ui_resource_path("LatoLatin-Regular.ttf");
  setup_ui::View view(session, options);
  if (!view.open()) {
    x2_log_error("install setup: the setup screen could not open: %s\n",
                 view.last_error().c_str());
    return false;
  }
  while (view.running()) {
    for (const setup_ui::Request &request : view.poll()) {
      if (request.kind == setup_ui::RequestKind::Browse) {
        char selection[kPathCapacity];
        const int chosen = install_picker_browse(selection, sizeof selection);
        if (chosen < 0) {
          x2_log_error("install setup: the file dialog failed: %s\n",
                       SDL_GetError());
        } else if (chosen > 0) {
          std::string error;
          session.reset();
          session.add_selected({std::filesystem::path{selection}}, error);
        }
      } else if (request.kind == setup_ui::RequestKind::Cancel) {
        view.finish();
      }
    }
    session.validate_if_ready();
    if (session.status() == setup_ui::Status::Accepted) {
      /* One more frame, so the player sees the install accepted. */
      view.frame();
      view.close();
      return true;
    }
    view.frame();
  }
  view.close();
  return false;
}

#endif

} // namespace

int install_setup_resolve_env(int appimage_product, int have_install_dir) {
  const char *current =
      config_override_get(x2::config::ConfigOverride::GamePcDir);
  if (!appimage_product || have_install_dir || (current && current[0]))
    return 0;
  char saved[kPathCapacity];
  std::string directory;
  if (install_picker_saved_directory(saved, sizeof saved)) {
    directory = saved;
  } else if (ask(directory)) {
    install_picker_remember_directory(directory.c_str());
  } else {
    x2_log_error("x2native: no PC installation was selected; exiting.\n");
    return 1;
  }
  if (config_override_set(x2::config::ConfigOverride::GamePcDir,
                          directory.c_str(), 1) != 0) {
    x2_log_error("x2native: could not set the selected installation: %s\n",
                 strerror(errno));
    return 2;
  }
  return 0;
}

} // namespace x2::native
