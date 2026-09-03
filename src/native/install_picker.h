#ifndef X2_INSTALL_PICKER_H
#define X2_INSTALL_PICKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Choose and remember the user's read-only PC installation for the AppImage
 * first-run flow. The returned directory is owned by this module. */
int x2_install_picker_choose(const char **directory);

/* AppImage first-run gate for main(): when this is the packaged product route
 * and GAME_PC_DIR is neither configured nor already in the environment, run
 * the chooser and publish the selection into GAME_PC_DIR. Returns 0 to
 * continue, 1 if the player selected nothing (caller exits 0), 2 on a publish
 * error (caller exits 1). Any other launch shape returns 0 untouched. */
int x2_install_picker_resolve_env(int appimage_product, int have_install_dir);

/* Pure validation seam used by the shipping picker and its focused test. */
int x2_install_picker_directory_from_executable(const char *path,
                                                char *directory,
                                                unsigned capacity);

/* Validate a player selection before the platform publishes it. ZIP installs
 * are extracted into the supplied fresh private destination; folder installs
 * ignore that argument. */
int x2_install_picker_prepare_selection(const char *selection,
                                        const char *archive_destination,
                                        char *reason, unsigned reason_capacity);

#ifdef __cplusplus
}
#endif

#endif /* X2_INSTALL_PICKER_H */
