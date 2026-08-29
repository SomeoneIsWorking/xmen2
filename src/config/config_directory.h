#ifndef X2_CONFIG_DIRECTORY_H
#define X2_CONFIG_DIRECTORY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Return the per-user configuration directory for this port. The path is
 * process-lifetime storage owned by this module. */
const char *x2_config_directory(void);

/* Create the directory and any missing parents. */
int x2_config_directory_ensure(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_CONFIG_DIRECTORY_H */
