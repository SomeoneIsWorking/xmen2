#include "autosave_runtime.h"

#include "../input/gameplay_control.h"
#include "autosave_format.h"
#include "autosave_notice.hpp"
#include "autosave_policy.h"
#include "autosave_storage.h"
#include "campaign_snapshot.h"
#include "guest_clock.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "guest_modules.h"
#include "save_catalog.h"
#include "save_directory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <errno.h>
#include <inttypes.h>
#include <lucent/log_c.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace x2::native {

namespace {

enum { MANAGER_RVA = 0x0035cbc0u, MANAGER_MODE = 0xd4u };

enum AutosaveLastResult {
  AUTOSAVE_LAST_NONE = 0,
  AUTOSAVE_LAST_SERIALIZER_FAILED,
  AUTOSAVE_LAST_HEADER_FAILED,
  AUTOSAVE_LAST_DIRECTORY_FAILED,
  AUTOSAVE_LAST_PUBLISH_FAILED,
  AUTOSAVE_LAST_SUCCEEDED
};

constexpr const char *RESULT[] = {"none",           "serializer-failed",
                                  "header-failed",  "directory-failed",
                                  "publish-failed", "succeeded"};
constexpr const char *CHECKPOINT[] = {"none", "extraction pad"};

x2::save::AutosavePolicy g_policy;

uint32_t g_exe;

x2::native::CampaignSnapshot *g_snapshot;

uint32_t g_last_manager_mode;

X2GameplayControl g_last_control;

AutosaveLastResult g_last_result;

int g_last_errno;

char g_last_leaf[x2::save::kSaveLeafCapacity];

int g_initialized;

void initialize(void) {
  if (g_initialized)
    return;
  x2::save::autosave_policy_init(&g_policy);
  g_initialized = 1;
}

int serialize_snapshot(const CPU *source) {
  if (!g_snapshot)
    g_snapshot = x2::native::campaign_snapshot_create();
  return x2::native::campaign_snapshot_capture(g_snapshot, source);
}

int publish_snapshot(const CPU *source) {
  unsigned char header[x2::save::kSaveHeaderBytes];
  const char *directory;

  g_last_errno = 0;
  if (!serialize_snapshot(source)) {
    g_last_result = AUTOSAVE_LAST_SERIALIZER_FAILED;
    return 0;
  }
  if (!x2::save::autosave_header_from_payload(
          guest_memory_as<const unsigned char>(
              x2::native::campaign_snapshot_address(g_snapshot)),
          x2::native::kCampaignSnapshotPayloadBytes, header)) {
    g_last_result = AUTOSAVE_LAST_HEADER_FAILED;
    return 0;
  }
  directory = x2::save::retail_save_directory();
  if (!directory ||
      !x2::save::save_catalog_next_autosave(directory, g_last_leaf)) {
    g_last_result = AUTOSAVE_LAST_DIRECTORY_FAILED;
    return 0;
  }
  if (!x2::save::autosave_storage_publish(
          directory, g_last_leaf, header,
          guest_memory_const_pointer(
              x2::native::campaign_snapshot_address(g_snapshot)),
          x2::native::kCampaignSnapshotPayloadBytes,
          x2::save::AutosaveStorageFault::None)) {
    g_last_errno = errno;
    g_last_result = AUTOSAVE_LAST_PUBLISH_FAILED;
    return 0;
  }
  g_last_result = AUTOSAVE_LAST_SUCCEEDED;
  return 1;
}

} // namespace

void autosave_runtime_extraction_pad() {
  initialize();
  const bool taken =
      x2::save::autosave_policy_extraction_pad(&g_policy, guest_clock_now_s());
  lucent_log_info("autosave", "extraction pad: %s",
                  taken ? "checkpoint scheduled"
                        : "within the autosave cooldown, no save");
}

void autosave_runtime_menu_show(void) {
  initialize();
  x2::save::autosave_policy_menu_show(&g_policy);
}

void autosave_runtime_poll(CPU *cpu) {
  x2::save::AutosaveCheckpoint checkpoint;
  x2::save::AutosavePollResult result;
  int succeeded;

  initialize();
  g_exe = x2::native::guest_exe_base();
  if (!cpu || !g_exe)
    return;
  g_last_manager_mode = RD32(g_exe + MANAGER_RVA + MANAGER_MODE);
  g_last_control = x2_gameplay_control_state(guest_clock_now_s());
  result = x2::save::autosave_policy_poll(&g_policy, g_last_manager_mode,
                                          g_last_control == kX2ControlActive,
                                          &checkpoint);
  if (result != x2::save::AutosavePollResult::Fire)
    return;
  succeeded = publish_snapshot(cpu);
  x2::save::autosave_policy_finish(&g_policy, checkpoint.id, succeeded);
  if (succeeded) {
    x2::save::autosave_notice().saved();
  } else {
    x2::save::autosave_notice().failed();
  }
  lucent_log_info("autosave", "%s %s (%s), errno %d", RESULT[g_last_result],
                  g_last_leaf, CHECKPOINT[static_cast<int>(checkpoint.kind)],
                  g_last_errno);
}

size_t autosave_runtime_report(char *out, size_t capacity) {
  int count;

  initialize();
  if (!out || !capacity)
    return 0;
  count =
      snprintf(out, capacity,
               "autosave scheduled=%" PRIu64 " cooled=%" PRIu64
               " cancelled-menu=%" PRIu64
               " idle-polls=%u manager-mode=%u control=%s deferred=%" PRIu64
               " control-deferred=%" PRIu64 " attempts=%" PRIu64 "/%" PRIu64
               " success=%" PRIu64 "/%" PRIu64 " fail=%" PRIu64 "/%" PRIu64
               " pending=%d active=%d last=%s leaf=%s errno=%d notice=\"%s\"\n",
               g_policy.scheduled, g_policy.cooled, g_policy.cancelled_menu,
               g_policy.idle_polls, g_last_manager_mode,
               x2_gameplay_control_name(static_cast<int>(g_last_control)),
               g_policy.deferred_polls, g_policy.control_deferred_polls,
               g_policy.attempts, g_policy.scheduled, g_policy.successes,
               g_policy.attempts, g_policy.failures, g_policy.attempts,
               g_policy.has_pending, g_policy.has_active, RESULT[g_last_result],
               g_last_leaf[0] ? g_last_leaf : "none", g_last_errno,
               x2::save::autosave_notice().text().c_str());
  if (count < 0 || (size_t)count >= capacity)
    return 0;
  return (size_t)count;
}

} // namespace x2::native
