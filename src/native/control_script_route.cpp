#include "control_script_route.hpp"

#include "control.h"
#include "control_console.h"
#include "control_query.h"
#include "directory_create.h"
#include "live_session.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace x2::control {
namespace {

/* Guest script names resolve under scripts/; this is ours inside the overlay.
 */
inline constexpr const char *kScriptDirectory = "scripts/x2/control";

unsigned g_scripts;

bool write_script(const std::string &path, const char *text) {
  FILE *file = std::fopen(path.c_str(), "wb");
  if (!file) {
    return false;
  }
  bool written = std::fputs(text, file) >= 0 && std::fputc('\n', file) != EOF;
  return std::fclose(file) == 0 && written;
}

} // namespace

void script_route(x2::native::Socket fd, const char *query) {
  std::vector<char> text(std::strlen(query) + 1u);
  if (!control_query_arg(query, "text", text.data(), text.size()) || !text[0]) {
    control_reply_text(
        fd, 400, "Bad Request",
        "no script. Use /script?text=spawn(...) with URL-encoded code\n");
    return;
  }
  std::string directory =
      std::string(x2::native::live_session_overlay()) + "/" + kScriptDirectory;
  if (!x2::config::directory_create(directory.c_str())) {
    control_reply_text(fd, 500, "Internal Server Error",
                       "cannot create %s: %s\n", directory.c_str(),
                       std::strerror(errno));
    return;
  }
  unsigned number = ++g_scripts;
  std::string path = directory + "/" + std::to_string(number) + ".py";
  if (!write_script(path, text.data())) {
    control_reply_text(fd, 500, "Internal Server Error",
                       "cannot write %s: %s\n", path.c_str(),
                       std::strerror(errno));
    return;
  }
  x2::native::control_console_run(fd, "runscript x2/control/" +
                                          std::to_string(number));
}

} // namespace x2::control
