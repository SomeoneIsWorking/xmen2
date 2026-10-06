#ifndef X2_INSTALL_VALIDATION_H
#define X2_INSTALL_VALIDATION_H

/* Validate the selected executable, the original images the native loader
 * maps, and title-owned content sentinels needed for a launchable install.
 * `reason` is optional. */
int x2_install_validate_executable(const char *executable, char *reason,
                                   unsigned reason_capacity);

#endif /* X2_INSTALL_VALIDATION_H */
