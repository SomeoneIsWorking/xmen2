#ifndef X2_BOOT_MODE_RUNTIME_H
#define X2_BOOT_MODE_RUNTIME_H

#include "boot_mode_policy.h"

/* Resolve the persistent boot request once. The latest leaf remains owned by
   save_catalog and is exposed for the retail Continue dispatcher to consume. */
const X2BootModeDecision *
x2_boot_mode_runtime_prepare(X2BootMode requested,
                             const char *retail_save_directory);
const char *x2_boot_mode_runtime_continue_leaf(void);
int x2_boot_mode_runtime_catalog_failed(void);
void x2_boot_mode_runtime_continue_started(void);

#endif
