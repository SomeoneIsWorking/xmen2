#ifndef X2_BOOT_MODE_H
#define X2_BOOT_MODE_H

typedef enum { X2_BOOT_NORMAL = 0, X2_BOOT_MENU, X2_BOOT_CONTINUE } X2BootMode;

const char *x2_boot_mode_name(X2BootMode mode);
const char *x2_boot_mode_label(X2BootMode mode);
int x2_boot_mode_parse(const char *text, X2BootMode *mode);

#endif
