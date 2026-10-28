#include "control_script_route.hpp"

#include "control.h"
#include "control_console.h"
#include "control_query.h"
#include "directory_create.h"
#include "host_dir_cache.h"
#include "live_session.h"

#include <cerrno>
#include <cstddef>
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

/* The retail loader splits a script file on "\r\n" only (FUN_004da450 with
   0x0068d350); any other break leaves the lines as one statement. */
std::string retail_lines(const char *text) {
  std::string lines;
  for (const char *at = text; *at; ++at) {
    if (*at != '\r' && *at != '\n') {
      lines += *at;
      continue;
    }
    lines += "\r\n";
    if (at[0] == '\r' && at[1] == '\n') {
      ++at;
    }
  }
  lines += "\r\n";
  return lines;
}

bool write_script(const std::string &path, const char *text) {
  FILE *file = std::fopen(path.c_str(), "wb");
  if (!file) {
    return false;
  }
  const std::string lines = retail_lines(text);
  bool written =
      std::fwrite(lines.data(), 1, lines.size(), file) == lines.size();
  return std::fclose(file) == 0 && written;
}

/* The guest resolves the new names through host_dir_cache, which cannot see
   them appear: drop every listing from the overlay root down to the file. */
void forget_listings(const std::string &root, const std::string &path) {
  for (std::size_t slash = path.find('/', root.size() + 1);
       slash != std::string::npos; slash = path.find('/', slash + 1)) {
    host_dir_forget_for(path.substr(0, slash).c_str());
  }
  host_dir_forget_for(path.c_str());
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
  const std::string root = x2::native::live_session_overlay();
  std::string directory = root + "/" + kScriptDirectory;
  if (!x2::config::directory_create(directory.c_str())) {
    control_reply_text(fd, 500, "Internal Server Error",
                       "cannot create %s: %s\n", directory.c_str(),
                       std::strerror(errno));
    return;
  }
  unsigned number = ++g_scripts;
  std::string path = directory + "/" + std::to_string(number) + ".py";
  const bool written = write_script(path, text.data());
  const int error = errno;
  forget_listings(root, path);
  if (!written) {
    control_reply_text(fd, 500, "Internal Server Error",
                       "cannot write %s: %s\n", path.c_str(),
                       std::strerror(error));
    return;
  }
  x2::native::control_console_run(fd, "runscript x2/control/" +
                                          std::to_string(number));
}

} // namespace x2::control
