#ifndef X2_INSTALL_VALIDATION_H
#define X2_INSTALL_VALIDATION_H

#ifdef __cplusplus
extern "C" {
#endif

/* Validate the selected executable and the exact original images the native
 * loader will map from its containing directory. `reason` is optional. */
int x2_install_validate_executable(const char *executable, char *reason,
                                   unsigned reason_capacity);

#ifdef __cplusplus
}
#endif

#endif /* X2_INSTALL_VALIDATION_H */
