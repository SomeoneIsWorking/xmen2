#include "autosave_runtime.h"

#include "autosave_format.h"
#include "autosave_policy.h"
#include "autosave_storage.h"
#include "conversation_resume.h"
#include "guest_heap.h"
#include "save_directory.h"
#include "save_trace_runtime.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    EXE_PREFERRED = 0x00400000u,
    FN_GAME_OWNER = 0x0006dce0u,
    MANAGER_RVA = 0x0035cbc0u,
    MANAGER_MODE = 0xd4u,
    SERIALIZER_VSLOT = 0x208u,
    SNAPSHOT_PAYLOAD_BYTES = 0x2fc00u,
    SNAPSHOT_OBJECT_BYTES = 0x2fc78u,
    SNAPSHOT_SELF = 0x2fc00u,
    SNAPSHOT_HEADER_FLAG_A = 0x2fc04u,
    SNAPSHOT_HEADER_FLAG_B = 0x2fc44u
};

typedef enum {
    AUTOSAVE_LAST_NONE = 0,
    AUTOSAVE_LAST_SERIALIZER_FAILED,
    AUTOSAVE_LAST_HEADER_FAILED,
    AUTOSAVE_LAST_DIRECTORY_FAILED,
    AUTOSAVE_LAST_PUBLISH_FAILED,
    AUTOSAVE_LAST_SUCCEEDED
} AutosaveLastResult;

static X2AutosavePolicy g_policy;
static uint32_t g_exe;
static uint32_t g_snapshot;
static uint32_t g_last_manager_mode;
static AutosaveLastResult g_last_result;
static int g_last_errno;
static int g_initialized;

void fn_XMen2_00484ce0(CPU *C);

static void initialize(void)
{
    if (g_initialized) return;
    x2_autosave_policy_init(&g_policy);
    g_initialized = 1;
}

static uint32_t exe_base(void)
{
    const X86Module *module;
    if (g_exe) return g_exe;
    for (module = x86_modules(); module; module = module->next)
        if (module->preferred == EXE_PREFERRED && *module->base) {
            g_exe = *module->base;
            break;
        }
    return g_exe;
}

static int serialize_snapshot(const CPU *source)
{
    CPU call = *source;
    uint32_t owner;
    uint32_t target;

    if (!g_snapshot) g_snapshot = guest_malloc(SNAPSHOT_OBJECT_BYTES);
    if (!g_snapshot) return 0;
    memset((void *)(uintptr_t)g_snapshot, 0, SNAPSHOT_OBJECT_BYTES);
    WR32(g_snapshot + SNAPSHOT_SELF, g_snapshot);
    WR8(g_snapshot + SNAPSHOT_HEADER_FLAG_A, 0u);
    WR8(g_snapshot + SNAPSHOT_HEADER_FLAG_B, 0u);

    x86_guest_call_args(&call, g_exe + FN_GAME_OWNER, 0u);
    owner = call.eax;
    if (!owner || !RD32(owner)) return 0;
    target = RD32(RD32(owner) + SERIALIZER_VSLOT);
    if (!target) return 0;
    call = *source;
    call.esp -= 4u; WR32(call.esp, g_snapshot);
    call.ecx = owner;
    x86_guest_call_args(&call, target, 4u);
    return 1;
}

static int publish_snapshot(const CPU *source)
{
    unsigned char header[X2_SAVE_HEADER_BYTES];
    const char *directory;

    g_last_errno = 0;
    if (!serialize_snapshot(source)) {
        g_last_result = AUTOSAVE_LAST_SERIALIZER_FAILED;
        return 0;
    }
    if (!x2_autosave_header_from_payload(
            (const unsigned char *)(uintptr_t)g_snapshot,
            SNAPSHOT_PAYLOAD_BYTES, header)) {
        g_last_result = AUTOSAVE_LAST_HEADER_FAILED;
        return 0;
    }
    directory = x2_retail_save_directory();
    if (!directory) {
        g_last_result = AUTOSAVE_LAST_DIRECTORY_FAILED;
        return 0;
    }
    if (!x2_autosave_storage_publish(
            directory, header, (const void *)(uintptr_t)g_snapshot,
            SNAPSHOT_PAYLOAD_BYTES, X2_AUTOSAVE_FAULT_NONE)) {
        g_last_errno = errno;
        g_last_result = AUTOSAVE_LAST_PUBLISH_FAILED;
        return 0;
    }
    g_last_result = AUTOSAVE_LAST_SUCCEEDED;
    return 1;
}

void x2_autosave_runtime_map_return(int succeeded)
{
    initialize();
    x2_autosave_policy_map_return(&g_policy, succeeded);
}

void x2_autosave_runtime_menu_show(void)
{
    initialize();
    x2_autosave_policy_menu_show(&g_policy);
}

void x2_autosave_runtime_poll(CPU *cpu)
{
    X2AutosaveCheckpoint checkpoint;
    X2AutosavePollResult result;
    int succeeded;

    initialize();
    if (!cpu || !exe_base()) return;
    g_last_manager_mode = RD32(g_exe + MANAGER_RVA + MANAGER_MODE);
    result = x2_autosave_policy_poll(&g_policy, g_last_manager_mode,
                                     &checkpoint);
    if (result != X2_AUTOSAVE_POLL_FIRE) return;
    succeeded = publish_snapshot(cpu);
    x2_autosave_policy_finish(&g_policy, checkpoint.id, succeeded);
}

size_t x2_autosave_runtime_report(char *out, size_t capacity)
{
    static const char *const RESULT[] = {
        "none", "serializer-failed", "header-failed", "directory-failed",
        "publish-failed", "succeeded"
    };
    int count;

    initialize();
    if (!out || !capacity) return 0;
    count = snprintf(
        out, capacity,
        "autosave map-success=%" PRIu64 "/%" PRIu64
        " scheduled=%" PRIu64 " cancelled-menu=%" PRIu64
        " idle-polls=%u manager-mode=%u deferred=%" PRIu64
        " attempts=%" PRIu64 "/%" PRIu64
        " success=%" PRIu64 "/%" PRIu64
        " fail=%" PRIu64 "/%" PRIu64
        " pending=%d active=%d last=%s errno=%d\n",
        g_policy.successful_map_returns, g_policy.map_returns,
        g_policy.scheduled, g_policy.cancelled_menu, g_policy.idle_polls,
        g_last_manager_mode, g_policy.deferred_polls,
        g_policy.attempts, g_policy.scheduled,
        g_policy.successes, g_policy.attempts,
        g_policy.failures, g_policy.attempts,
        g_policy.has_pending, g_policy.has_active,
        RESULT[g_last_result], g_last_errno);
    if (count < 0 || (size_t)count >= capacity) return 0;
    return (size_t)count;
}

static void x2_autosave_override_00484ce0(CPU *C)
{
    uint32_t map = C->ecx;
    int succeeded;

    fn_XMen2_00484ce0(C);
    succeeded = (C->eax & 0xffu) != 0u;
    x2_save_trace_map_return(map, succeeded);
    x2_autosave_runtime_map_return(succeeded);
    x2_conversation_resume_map_return(succeeded);
}

__attribute__((constructor))
static void x2_autosave_register(void)
{
    x86_register_override("XMen2.exe", 0x00484ce0u,
                          x2_autosave_override_00484ce0);
}
