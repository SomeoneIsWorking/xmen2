/* OS user-config directory resolution, shared by settings and save storage. */
#include "config_directory.h"

#include <lucent/platform_c.h>

const char *x2_config_directory(void)
{
    return lucent_platform_user_data_directory("xmen2");
}

int x2_config_directory_ensure(void)
{
    return lucent_platform_ensure_user_data_directory("xmen2");
}
