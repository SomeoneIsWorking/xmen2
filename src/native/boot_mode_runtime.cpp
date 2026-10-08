#include "boot_mode_runtime.h"

#include "save_catalog.h"

#include <string.h>

namespace x2::native {

namespace {

BootModeDecision g_decision;

X2SaveCandidate g_latest;

int g_ready;

int g_continue_pending;

int g_catalog_failed;

} // namespace

const BootModeDecision *
boot_mode_runtime_prepare(X2BootMode requested,
                          const char *retail_save_directory) {
  int latest_available = 0;
  if (g_ready)
    return &g_decision;
  memset(&g_latest, 0, sizeof g_latest);
  if (requested == X2_BOOT_CONTINUE) {
    int result = -1;
    if (retail_save_directory)
      result = x2_save_catalog_latest(retail_save_directory, &g_latest);
    latest_available = result == 1;
    g_catalog_failed = result < 0;
  }
  g_decision = boot_mode_decide(requested, latest_available);
  g_continue_pending = g_decision.effective == X2_BOOT_CONTINUE;
  g_ready = 1;
  return &g_decision;
}

int boot_mode_runtime_catalog_failed(void) { return g_catalog_failed; }

const char *boot_mode_runtime_continue_leaf(void) {
  return g_continue_pending ? g_latest.leaf : NULL;
}

void boot_mode_runtime_continue_started(void) { g_continue_pending = 0; }

} // namespace x2::native
