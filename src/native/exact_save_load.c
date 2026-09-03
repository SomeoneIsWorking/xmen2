#include "exact_save_load.h"

#include "guest_heap.h"
#include "guest_memory.h"
#include "save_catalog.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "guest_body.h"

enum {
    FN_SAVE_MANAGER = 0x000b2880u,
    FN_STORAGE = 0x0015e9a0u,
    FN_SET_MODE = 0x000aeb80u,
    FN_SET_DEVICE = 0x000aece0u,
    FN_CHOOSE_FILE = 0x000ae850u,
    FN_READ_HEADER = 0x0015f580u,
    FN_READ_LEAF = 0x0015fcd0u,
    SAVE_MODE_IDLE = 0u,
    SAVE_MODE_LOAD = 3u,
    SAVE_DEVICE_PC = 0u,
    MANAGER_MODE = 0xd4u,
    MANAGER_STATE = 0xd8u,
    MANAGER_METADATA = 0xe4u,
    METADATA_STRIDE = 0xa8u,
    MANAGER_METADATA_SLOTS = 10u
};

static uint32_t g_leaf_guest;
static uint32_t g_pending_exe;
static X2ExactSaveLoadOwner g_owner;
static X2ExactSaveLoadCompletion g_completion;


static uint32_t guest_call0(const CPU *source, uint32_t target)
{
    CPU call = *source;
    x86_guest_call_args(&call, target, 0u);
    return call.eax;
}

static int prepare_leaf(const char *leaf)
{
    size_t length;

    if (!leaf || g_owner != X2_EXACT_SAVE_LOAD_NONE) return 0;
    length = strlen(leaf);
    if (length == 0u || length >= X2_SAVE_LEAF_CAPACITY) return 0;
    if (!g_leaf_guest) g_leaf_guest = guest_malloc(X2_SAVE_LEAF_CAPACITY);
    if (!g_leaf_guest) return 0;
    memcpy(guest_memory_pointer(g_leaf_guest), leaf, length + 1u);
    return 1;
}

static int read_prepared_header(const CPU *source, uint32_t exe,
                                uint32_t metadata)
{
    CPU call;
    uint32_t storage = guest_call0(source, exe + FN_STORAGE);

    if (!storage || !metadata) return 0;
    memset(guest_memory_pointer(metadata), 0, METADATA_STRIDE);
    call = *source;
    call.esp -= 4u; WR32(call.esp, metadata);
    call.esp -= 4u; WR32(call.esp, g_leaf_guest);
    call.esp -= 4u; WR32(call.esp, SAVE_DEVICE_PC);
    call.ecx = storage;
    x86_guest_call_args(&call, exe + FN_READ_HEADER, 12u);
    return (call.eax & 0xffu) != 0u;
}

static void set_manager_mode(const CPU *source, uint32_t exe,
                             uint32_t manager, uint32_t mode)
{
    CPU call = *source;

    call.esp -= 4u; WR32(call.esp, mode);
    call.ecx = manager;
    x86_guest_call_args(&call, exe + FN_SET_MODE, 4u);
}

int x2_exact_save_load_read_header(const CPU *source, uint32_t exe,
                                   const char *leaf, uint32_t metadata)
{
    return source && exe && prepare_leaf(leaf)
        && read_prepared_header(source, exe, metadata);
}

int x2_exact_save_load_start(const CPU *source, uint32_t exe,
                             const char *leaf, unsigned staging_slot,
                             X2ExactSaveLoadOwner owner,
                             X2ExactSaveLoadCompletion completion)
{
    CPU call;
    uint8_t previous_metadata[METADATA_STRIDE];
    uint32_t manager;
    uint32_t entry;
    uint32_t expected_mode;
    int entered_load_mode = 0;

    if (!source || !exe
        || (owner != X2_EXACT_SAVE_LOAD_CONTINUE
            && owner != X2_EXACT_SAVE_LOAD_MENU)
        || staging_slot >= MANAGER_METADATA_SLOTS || !prepare_leaf(leaf))
        return 0;
    manager = guest_call0(source, exe + FN_SAVE_MANAGER);
    if (!manager) return 0;
    expected_mode = owner == X2_EXACT_SAVE_LOAD_CONTINUE
        ? SAVE_MODE_IDLE : SAVE_MODE_LOAD;
    if (RD32(manager + MANAGER_MODE) != expected_mode) return 0;

    /* Continue owns the idle -> load transition. Preserve that evidenced
       ordering: SetMode resets the manager transaction before any header I/O.
       The Load Game menu already owns mode 3, so it must not reset itself. */
    if (owner == X2_EXACT_SAVE_LOAD_CONTINUE) {
        set_manager_mode(source, exe, manager, SAVE_MODE_LOAD);
        if (RD32(manager + MANAGER_MODE) != SAVE_MODE_LOAD) {
            set_manager_mode(source, exe, manager, SAVE_MODE_IDLE);
            return 0;
        }
        entered_load_mode = 1;
    }

    entry = manager + MANAGER_METADATA + staging_slot * METADATA_STRIDE;
    memcpy(previous_metadata, guest_memory_const_pointer(entry),
           sizeof previous_metadata);
    if (!read_prepared_header(source, exe, entry)) {
        memcpy(guest_memory_pointer(entry), previous_metadata,
               sizeof previous_metadata);
        if (entered_load_mode)
            set_manager_mode(source, exe, manager, SAVE_MODE_IDLE);
        return 0;
    }
    call = *source;
    call.esp -= 4u; WR32(call.esp, SAVE_DEVICE_PC);
    call.ecx = manager;
    x86_guest_call_args(&call, exe + FN_SET_DEVICE, 4u);
    call = *source;
    call.esp -= 4u; WR32(call.esp, staging_slot);
    call.ecx = manager;
    x86_guest_call_args(&call, exe + FN_CHOOSE_FILE, 4u);
    if (RD32(manager + MANAGER_MODE) != SAVE_MODE_LOAD
        || RD32(manager + MANAGER_STATE) != 0x1cu) {
        fprintf(stderr, "exact-save-load: retail manager refused mode3/state1c "
                        "dispatch (mode=%u state=%u)\n",
                RD32(manager + MANAGER_MODE), RD32(manager + MANAGER_STATE));
        if (entered_load_mode)
            set_manager_mode(source, exe, manager, SAVE_MODE_IDLE);
        memcpy(guest_memory_pointer(entry), previous_metadata,
               sizeof previous_metadata);
        return 0;
    }
    g_pending_exe = exe;
    g_owner = owner;
    g_completion = completion;
    return 1;
}

static int redirect_pending_load(CPU *cpu)
{
    X2ExactSaveLoadCompletion completion = g_completion;
    X2ExactSaveLoadOwner pending = g_owner;
    uint32_t exe = g_pending_exe;
    int succeeded;

    if (!cpu || !exe || pending == X2_EXACT_SAVE_LOAD_NONE) return 0;
    g_pending_exe = 0u;
    g_owner = X2_EXACT_SAVE_LOAD_NONE;
    g_completion = NULL;
    WR32(cpu->esp + 8u, g_leaf_guest);
    x86_dispatch(cpu, exe + FN_READ_LEAF);
    succeeded = (cpu->eax & 0xffu) != 0u;
    if (completion) completion(succeeded);
    return 1;
}

static void x2_override_0055ff00(CPU *C)
{
    if (!redirect_pending_load(C))
        x86_guest_body(C, "XMen2.exe", 0x0055ff00u);
}

__attribute__((constructor))
static void x2_exact_save_load_register(void)
{
    x86_register_override("XMen2.exe", 0x0055ff00u,
                          x2_override_0055ff00);
}
