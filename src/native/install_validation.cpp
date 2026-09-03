#include "install_validation.h"

#include "install_requirements.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <strings.h>

namespace {

bool set_reason(char *reason, unsigned capacity, const char *format,
                const char *detail) {
  if (reason && capacity)
    std::snprintf(reason, capacity, format, detail);
  return false;
}

enum class RequiredFile {
  Found,
  Missing,
  Unreadable,
};

RequiredFile regular_file_named(const std::filesystem::path &directory,
                                const char *name) {
  std::error_code error;
  for (std::filesystem::directory_iterator it(
           directory,
           std::filesystem::directory_options::skip_permission_denied, error),
       end;
       it != end && !error; it.increment(error)) {
    const bool regular = it->is_regular_file(error);
    if (error)
      return RequiredFile::Unreadable;
    if (!regular)
      continue;
    if (strcasecmp(it->path().filename().string().c_str(), name) == 0)
      return RequiredFile::Found;
  }
  return error ? RequiredFile::Unreadable : RequiredFile::Missing;
}

RequiredFile regular_file_at(const std::filesystem::path &directory,
                             const char *relative_path) {
  std::filesystem::path current = directory;
  std::string component;
  const char *cursor = relative_path;

  while (cursor && *cursor) {
    const char *separator = std::strchr(cursor, '/');
    component.assign(cursor,
                     separator ? separator - cursor : std::strlen(cursor));
    if (component.empty() || component == "." || component == "..")
      return RequiredFile::Missing;

    std::error_code error;
    bool found = false;
    for (std::filesystem::directory_iterator it(
             current,
             std::filesystem::directory_options::skip_permission_denied, error),
         end;
         it != end && !error; it.increment(error)) {
      const std::string name = it->path().filename().string();
      if (strcasecmp(name.c_str(), component.c_str()) != 0)
        continue;
      current = it->path();
      found = true;
      break;
    }
    if (error)
      return RequiredFile::Unreadable;
    if (!found)
      return RequiredFile::Missing;
    cursor = separator ? separator + 1 : nullptr;
  }

  std::error_code error;
  return std::filesystem::is_regular_file(current, error) && !error
             ? RequiredFile::Found
         : error ? RequiredFile::Unreadable
                 : RequiredFile::Missing;
}

} // namespace

extern "C" int x2_install_validate_executable(const char *executable,
                                              char *reason,
                                              unsigned reason_capacity) {
  if (reason && reason_capacity)
    reason[0] = 0;
  if (!executable || !*executable)
    return set_reason(reason, reason_capacity, "No XMen2.exe was selected.",
                      "");

  const std::filesystem::path image(executable);
  std::error_code error;
  if (!std::filesystem::is_regular_file(image, error) || error)
    return set_reason(reason, reason_capacity,
                      "Cannot read the selected XMen2.exe.", "");
  if (strcasecmp(image.filename().string().c_str(), "XMen2.exe") != 0)
    return set_reason(reason, reason_capacity, "That file is not XMen2.exe.",
                      "");

  const std::filesystem::path directory = image.parent_path();
  for (unsigned i = 0; i < X2_INSTALL_REQUIRED_IMAGE_COUNT; ++i) {
    const RequiredFile required =
        regular_file_named(directory, x2_install_required_images[i]);
    if (required == RequiredFile::Unreadable)
      return set_reason(reason, reason_capacity,
                        "Cannot inspect the directory beside XMen2.exe.", "");
    if (required == RequiredFile::Missing)
      return set_reason(reason, reason_capacity,
                        "Install is incomplete: missing %s beside XMen2.exe.",
                        x2_install_required_images[i]);
  }
  for (unsigned i = 0; i < X2_INSTALL_REQUIRED_CONTENT_COUNT; ++i) {
    const RequiredFile required =
        regular_file_at(directory, x2_install_required_content[i]);
    if (required == RequiredFile::Unreadable)
      return set_reason(reason, reason_capacity,
                        "Cannot inspect the selected game content: %s.",
                        x2_install_required_content[i]);
    if (required == RequiredFile::Missing)
      return set_reason(reason, reason_capacity,
                        "Install is incomplete: missing game file %s.",
                        x2_install_required_content[i]);
  }
  return 1;
}
