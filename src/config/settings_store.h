#ifndef X2_SETTINGS_STORE_H
#define X2_SETTINGS_STORE_H

#include "settings.h"

/* Process-wide shipping settings. The parser remains independently testable;
   this layer only owns the save-directory path and publication lifetime. */
void x2_settings_store_init(void);
X2Settings *x2_settings_store(void);
int x2_settings_store_save(char *why, int whyn);
const char *x2_settings_store_path(void);

#endif
