/* OS user-config directory resolution, shared by settings and save storage. */
#include "config_directory.h"

#include <lucent/platform_c.h>

namespace x2::config {

const char *config_directory(void) {
  return lucent_platform_user_data_directory("xmen2");
}

int config_directory_ensure(void) {
  return lucent_platform_ensure_user_data_directory("xmen2");
}

} // namespace x2::config
