#pragma once

namespace x2::native {

/* Validate the selected executable, the original images the native loader
 * maps, and title-owned content sentinels needed for a launchable install.
 * `reason` is optional. */
int install_validate_executable(const char *executable, char *reason,
                                unsigned reason_capacity);

} // namespace x2::native
