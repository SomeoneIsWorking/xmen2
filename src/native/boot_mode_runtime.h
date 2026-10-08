#pragma once

#include "boot_mode_policy.h"

namespace x2::native {

/* Resolve the persistent boot request once. The latest leaf remains owned by
   save_catalog and is exposed for the retail Continue dispatcher to consume. */
const BootModeDecision *
boot_mode_runtime_prepare(x2::config::BootMode requested,
                          const char *retail_save_directory);
const char *boot_mode_runtime_continue_leaf();
int boot_mode_runtime_catalog_failed();
void boot_mode_runtime_continue_started();

} // namespace x2::native
