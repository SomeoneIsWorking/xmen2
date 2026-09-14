#include "web_request.hpp"

#include <cstring>

namespace x2::web {

launch_request classify_launch_request(int argc, char **argv) {
  bool importing = false;
  bool testing = false;
  for (int i = 1; i < argc; i++) {
    if (std::strcmp(argv[i], import_flag) == 0) {
      importing = true;
    } else if (std::strcmp(argv[i], gameplay_test_flag) == 0) {
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

bool is_entry_request(const char *arg) {
  return std::strcmp(arg, import_flag) == 0 ||
         std::strcmp(arg, gameplay_test_flag) == 0;
}

} // namespace x2::web
