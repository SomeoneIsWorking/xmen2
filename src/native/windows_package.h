#pragma once

namespace x2::native {

/* File the Windows ZIP ships beside the executable; its presence is what makes
 * a launch the packaged product rather than a developer build tree. */
inline constexpr const char *kWindowsPackageMarker = "xmen2-package.txt";

/* Packaged launch from `executable` (the .exe's full path). When the marker
 * sits beside it, publish the `ui` folder beside it as the UI resource
 * directory without overwriting a choice already made, and return 1. Any other
 * launch returns 0 and changes nothing. */
int windows_package_init_from(const char *executable);

/* The same for the running process on Windows; 0 on every other host. */
int windows_package_init();

} // namespace x2::native
