#include "autosave_runtime.h"

#include "../input/gameplay_control.h"
#include "autosave_format.h"
#include "autosave_policy.h"
#include "autosave_storage.h"
#include "boot_blackout.h"
#include "campaign_snapshot.h"
#include "guest_clock.h"
#include "guest_heap.h"
#include "guest_memory.h"
#include "guest_modules.h"
#include "lan_session.h"
#include "save_directory.h"
#include "save_trace_runtime.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include "guest_body.h"
#include <errno.h>
#include <inttypes.h>
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

x2::save::AutosavePolicy g_policy;

uint32_t g_exe;

x2::native::CampaignSnapshot *g_snapshot;

uint32_t g_last_manager_mode;

X2GameplayControl g_last_control;

AutosaveLastResult g_last_result;

int g_last_errno;

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
  if (!directory) {
    g_last_result = AUTOSAVE_LAST_DIRECTORY_FAILED;
    return 0;
  }
  if (!x2::save::autosave_storage_publish(
          directory, header,
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

void autosave_runtime_map_return(int succeeded) {
  initialize();
  x2::save::autosave_policy_map_return(&g_policy, succeeded);
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
}

size_t autosave_runtime_report(char *out, size_t capacity) {
  static const char *const RESULT[] = {"none",           "serializer-failed",
                                       "header-failed",  "directory-failed",
                                       "publish-failed", "succeeded"};
  int count;

  initialize();
  if (!out || !capacity)
    return 0;
  count = snprintf(out, capacity,
                   "autosave map-success=%" PRIu64 "/%" PRIu64
                   " scheduled=%" PRIu64 " cancelled-menu=%" PRIu64
                   " idle-polls=%u manager-mode=%u control=%s deferred=%" PRIu64
                   " control-deferred=%" PRIu64 " attempts=%" PRIu64 "/%" PRIu64
                   " success=%" PRIu64 "/%" PRIu64 " fail=%" PRIu64 "/%" PRIu64
                   " pending=%d active=%d last=%s errno=%d\n",
                   g_policy.successful_map_returns, g_policy.map_returns,
                   g_policy.scheduled, g_policy.cancelled_menu,
                   g_policy.idle_polls, g_last_manager_mode,
                   x2_gameplay_control_name(static_cast<int>(g_last_control)),
                   g_policy.deferred_polls, g_policy.control_deferred_polls,
                   g_policy.attempts, g_policy.scheduled, g_policy.successes,
                   g_policy.attempts, g_policy.failures, g_policy.attempts,
                   g_policy.has_pending, g_policy.has_active,
                   RESULT[g_last_result], g_last_errno);
  if (count < 0 || (size_t)count >= capacity)
    return 0;
  return (size_t)count;
}

namespace {

void x2_autosave_override_00484ce0(CPU *C) {
  uint32_t map = C->reg[kX86pEcx];
  int succeeded;

  x86_guest_body(C, "XMen2.exe", 0x00484ce0u);
  succeeded = (C->reg[kX86pEax] & 0xffu) != 0u;
  x2::save::save_trace_map_return(map, succeeded);
  x2::native::lan_session_map_loaded(map, succeeded);
  autosave_runtime_map_return(succeeded);
  /* The boot's own destination load completes here: this is the signal the
     boot blackout waits for. Later zone loads arrive while the blackout is
     already closed and are no-ops to it. */
  if (succeeded)
    x2::presentation::boot_blackout_disarm("the boot's map load returned");
}

__attribute__((constructor)) static void x2_autosave_register(void) {
  x86_register_override("XMen2.exe", 0x00484ce0u,
                        x2_autosave_override_00484ce0);
}

} // namespace

} // namespace x2::native
