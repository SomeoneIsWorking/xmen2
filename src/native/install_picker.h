#pragma once

namespace x2::native {

/* The remembered installation directory, when it still holds a valid
 * XMen2.exe. Returns 1 and fills `directory`, else 0. */
int install_picker_saved_directory(char *directory, unsigned capacity);

/* Remember an accepted installation directory in the user configuration. */
void install_picker_remember_directory(const char *directory);

/* The OS open-file dialog. Returns 1 with the chosen path, 0 when the player
 * cancelled, -1 when the platform has no dialog. */
int install_picker_browse(char *selection, unsigned capacity);

/* Pure validation seam used by the shipping picker and its focused test. */
int install_picker_directory_from_executable(const char *path, char *directory,
                                             unsigned capacity);

/* Validate a player selection before the platform publishes it. ZIP installs
 * are extracted into the supplied fresh private destination; folder installs
 * ignore that argument. */
int install_picker_prepare_selection(const char *selection,
                                     const char *archive_destination,
                                     char *reason, unsigned reason_capacity);

/* Resolve the same validated selection for a platform whose first-run UI is
 * outside SDL. The caller publishes the returned directory only on success. */
int install_picker_resolve_selection(const char *selection,
                                     const char *archive_destination,
                                     char *directory, unsigned capacity,
                                     char *reason, unsigned reason_capacity);

} // namespace x2::native
