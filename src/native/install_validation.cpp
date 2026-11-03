#include "install_validation.h"

#include "host_files.h"
#include "install_requirements.h"
#include "pe_header.h"

#include "platform_strings.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

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

bool regular_file(const std::string &path) {
  struct stat status;
  return x2::native::host_stat(path.c_str(), &status) == 0 &&
         S_ISREG(status.st_mode);
}

/* The entry in `directory` named `name` case-insensitively, as `found`. */
RequiredFile entry_named(const std::string &directory, const std::string &name,
                         std::string &found) {
  x2::native::HostDirectory *listing =
      x2::native::host_opendir(directory.c_str());
  if (!listing)
    return RequiredFile::Unreadable;
  RequiredFile result = RequiredFile::Missing;
  while (const char *entry = x2::native::host_readdir(listing)) {
    if (strcasecmp(entry, name.c_str()) == 0) {
      found = directory + "/" + entry;
      result = RequiredFile::Found;
      break;
    }
  }
  x2::native::host_closedir(listing);
  return result;
}

RequiredFile regular_file_named(const std::string &directory,
                                const char *name) {
  std::string found;
  const RequiredFile entry = entry_named(directory, name, found);
  if (entry != RequiredFile::Found)
    return entry;
  return regular_file(found) ? RequiredFile::Found : RequiredFile::Missing;
}

RequiredFile regular_file_at(const std::string &directory,
                             const char *relative_path) {
  std::string current = directory;
  std::string component;
  const char *cursor = relative_path;

  while (cursor && *cursor) {
    const char *separator = std::strchr(cursor, '/');
    component.assign(cursor,
                     separator ? separator - cursor : std::strlen(cursor));
    if (component.empty() || component == "." || component == "..")
      return RequiredFile::Missing;
    std::string found;
    const RequiredFile entry = entry_named(current, component, found);
    if (entry != RequiredFile::Found)
      return entry;
    current = found;
    cursor = separator ? separator + 1 : nullptr;
  }
  return regular_file(current) ? RequiredFile::Found : RequiredFile::Missing;
}

} // namespace

namespace x2::native {
int install_validate_executable(const char *executable, char *reason,
                                unsigned reason_capacity) {
  if (reason && reason_capacity)
    reason[0] = 0;
  if (!executable || !*executable)
    return set_reason(reason, reason_capacity, "No XMen2.exe was selected.",
                      "");

  const std::string image(executable);
  if (!regular_file(image))
    return set_reason(reason, reason_capacity,
                      "Cannot read the selected XMen2.exe.", "");
  const size_t slash = image.find_last_of("/\\");
  const std::string directory =
      slash == std::string::npos ? "." : image.substr(0, slash);
  const std::string leaf =
      slash == std::string::npos ? image : image.substr(slash + 1);
  if (strcasecmp(leaf.c_str(), "XMen2.exe") != 0)
    return set_reason(reason, reason_capacity, "That file is not XMen2.exe.",
                      "");
  char header_reason[160];
  if (!x2::native::pe32_validate_file(executable, header_reason,
                                      sizeof header_reason))
    return set_reason(reason, reason_capacity, "%s", header_reason);

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
} // namespace x2::native
