#ifndef X2_INSTALL_PICKER_H
#define X2_INSTALL_PICKER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Choose and remember the user's read-only PC installation for the AppImage
 * first-run flow. The returned directory is owned by this module. */
int x2_install_picker_choose(const char **directory);

/* Pure validation seam used by the shipping picker and its focused test. */
int x2_install_picker_directory_from_executable(const char *path,
                                                char *directory,
                                                unsigned capacity);

#ifdef __cplusplus
}
#endif

#endif /* X2_INSTALL_PICKER_H */
