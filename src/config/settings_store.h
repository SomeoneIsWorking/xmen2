#pragma once

#include "settings.h"

namespace x2::config {

/* Process-wide shipping settings. The parser remains independently testable;
   this layer only owns the save-directory path and publication lifetime. */
void settings_store_init(void);
Settings *settings_store(void);
int settings_store_save(char *why, int whyn);
const char *settings_store_path(void);

} // namespace x2::config
