#include "web_request.hpp"

#include <cstddef>
#include <cstring>

namespace x2::web {

launch_request classify_launch_request(int argc, char **argv) {
  bool importing = false;
  bool testing = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], import_flag) == 0) {
      importing = true;
    } else if (std::strcmp(argv[i], gameplay_test_flag) == 0 ||
               std::strncmp(argv[i], gameplay_map_prefix,
                            std::strlen(gameplay_map_prefix)) == 0) {
      testing = true;
    }
  }
  if (importing && testing) {
    return launch_request::conflict;
  }
  if (importing) {
    return launch_request::import_archive;
  }
  if (testing) {
    return launch_request::gameplay_test;
  }
  return launch_request::none;
}

const char *gameplay_test_map(int argc, char **argv) {
  const char *map = deadzone_map;
  const std::size_t prefix = std::strlen(gameplay_map_prefix);
  for (int i = 1; i < argc; i++) {
    if (std::strncmp(argv[i], gameplay_map_prefix, prefix) != 0) {
      continue;
    }
    const char *value = argv[i] + prefix;
    if (*value != '\0') {
      map = value;
    }
  }
  return map;
}

bool is_entry_request(const char *arg) {
  return std::strcmp(arg, import_flag) == 0 ||
         std::strcmp(arg, gameplay_test_flag) == 0 ||
         std::strncmp(arg, gameplay_map_prefix,
                      std::strlen(gameplay_map_prefix)) == 0;
}

} // namespace x2::web
