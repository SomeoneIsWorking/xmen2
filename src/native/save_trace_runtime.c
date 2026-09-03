#include "save_trace_runtime.h"

#include "autosave_runtime.h"
#include "load_game_menu_runtime.h"
#include "save_trace.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "guest_body.h"

enum {
    X2_SAVE_MANAGER = 0x0075cbc0u,
    X2_SAVE_METADATA = 0x00a3bc40u,
    X2_MANAGER_MODE = 0xd4u,
    X2_MANAGER_STATE = 0xd8u,
    X2_MANAGER_DEVICE = 0xdcu,
    X2_MANAGER_SELECTION = 0xddu,
    X2_MANAGER_BUFFER = 0xe0u
};

static SaveTrace g_trace;
static int g_trace_enabled;


static uint32_t stack_arg(const CPU *C, unsigned index)
{
    return RD32(C->esp + 4u + index * 4u);
}

static void copy_guest_string(char *out, size_t capacity, uint32_t address)
{
    size_t i = 0;

    if (!capacity) return;
    if (address) {
        while (i + 1u < capacity) {
            uint8_t ch = RD8(address + (uint32_t)i);
            if (!ch) break;
            out[i++] = (char)ch;
        }
    }
    out[i] = 0;
}

static void mark_with_u32(SaveTracePoint point, SaveTraceAnswer answer,
                          const char *name, uint32_t value)
{
    char label[SAVE_TRACE_LABEL_CAPACITY];

    snprintf(label, sizeof label, "%s=0x%08x", name, value);
    save_trace_mark(&g_trace, point, answer, label);
}

static void capture_mode_cycle(uint32_t manager, uint32_t before)
{
    save_trace_mode_cycle(&g_trace, before,
                          manager ? RD32(manager + X2_MANAGER_MODE) : 0u);
}

static void x2_trace_005c9970(CPU *C)
{
    save_trace_mark(&g_trace, SAVE_TRACE_MENU_BUILD,
                    SAVE_TRACE_ANSWER_UNKNOWN, "CMenuMain::Build");
    x86_guest_body(C, "XMen2.exe", 0x005c9970u);
}

void x2_save_trace_menu_open(void)
{
    save_trace_mark(&g_trace, SAVE_TRACE_MENU_OPEN,
                    SAVE_TRACE_ANSWER_UNKNOWN, "CMenuMain::Show");
}

static void x2_trace_0055fcd0(CPU *C)
{
    char leaf[SAVE_TRACE_LABEL_CAPACITY];

    copy_guest_string(leaf, sizeof leaf, stack_arg(C, 1u));
    save_trace_mark(&g_trace, SAVE_TRACE_LOAD_0055FCD0,
                    SAVE_TRACE_ANSWER_UNKNOWN, leaf);
    x86_guest_body(C, "XMen2.exe", 0x0055fcd0u);
}

static void x2_trace_004aed10(CPU *C)
{
    uint32_t manager = C->ecx;

    save_trace_load_manager(
        &g_trace, RD8(X2_SAVE_METADATA) ? SAVE_TRACE_ANSWER_YES
                                       : SAVE_TRACE_ANSWER_NO,
        RD32(manager + X2_MANAGER_MODE), RD32(manager + X2_MANAGER_STATE),
        RD8(manager + X2_MANAGER_DEVICE),
        (uint32_t)(int32_t)(int8_t)RD8(manager + X2_MANAGER_SELECTION),
        RD32(manager + X2_MANAGER_BUFFER));
    x86_guest_body(C, "XMen2.exe", 0x004aed10u);
}

static void x2_trace_0046e2b0(CPU *C)
{
    char label[SAVE_TRACE_LABEL_CAPACITY];

    snprintf(label, sizeof label, "buffer=0x%08x kind=%u",
             stack_arg(C, 0u), stack_arg(C, 1u));
    save_trace_mark(&g_trace, SAVE_TRACE_LOAD_0046E2B0,
                    SAVE_TRACE_ANSWER_UNKNOWN, label);
    x86_guest_body(C, "XMen2.exe", 0x0046e2b0u);
}

static void x2_trace_0049f140(CPU *C)
{
    mark_with_u32(SAVE_TRACE_LOAD_0049F140, SAVE_TRACE_ANSWER_UNKNOWN,
                  "state", RD32(X2_SAVE_MANAGER + X2_MANAGER_STATE));
    x86_guest_body(C, "XMen2.exe", 0x0049f140u);
}

static void x2_trace_004aeb80(CPU *C)
{
    uint32_t manager = C->ecx;
    uint32_t before = RD32(manager + X2_MANAGER_MODE);

    mark_with_u32(SAVE_TRACE_SAVE_004AEB80, SAVE_TRACE_ANSWER_UNKNOWN,
                  "requested-mode", stack_arg(C, 0u));
    x86_guest_body(C, "XMen2.exe", 0x004aeb80u);
    capture_mode_cycle(manager, before);
}

static void x2_trace_004ae990(CPU *C)
{
    uint32_t manager = C->ecx;
    uint32_t before = RD32(manager + X2_MANAGER_MODE);

    x86_guest_body(C, "XMen2.exe", 0x004ae990u);
    capture_mode_cycle(manager, before);
}

static void x2_trace_004b15b0(CPU *C)
{
    uint32_t state = RD32(C->ecx + X2_MANAGER_STATE);

    mark_with_u32(SAVE_TRACE_SAVE_004B15B0,
                  state == 27u ? SAVE_TRACE_ANSWER_YES : SAVE_TRACE_ANSWER_NO,
                  "state", state);
    x86_guest_body(C, "XMen2.exe", 0x004b15b0u);
}

static void x2_trace_0046baf0(CPU *C)
{
    uint32_t caller = RD32(C->esp);

    mark_with_u32(SAVE_TRACE_SAVE_004B1746,
                  caller == 0x004b174cu ? SAVE_TRACE_ANSWER_YES
                                        : SAVE_TRACE_ANSWER_NO,
                  "return", caller);
    x86_guest_body(C, "XMen2.exe", 0x0046baf0u);
}

static void x2_trace_0055fe70(CPU *C)
{
    uint32_t caller = RD32(C->esp);

    mark_with_u32(SAVE_TRACE_SAVE_004B177A,
                  caller == 0x004b177du ? SAVE_TRACE_ANSWER_YES
                                        : SAVE_TRACE_ANSWER_NO,
                  "return", caller);
    x86_guest_body(C, "XMen2.exe", 0x0055fe70u);
}

void x2_save_trace_map_return(uint32_t map, int succeeded)
{
    char label[SAVE_TRACE_LABEL_CAPACITY];

    snprintf(label, sizeof label, "map=0x%08x", map);
    save_trace_mark(&g_trace, SAVE_TRACE_MAP_00484CE0,
                    succeeded ? SAVE_TRACE_ANSWER_YES
                              : SAVE_TRACE_ANSWER_NO,
                    label);
}

static void x2_trace_0049f860(CPU *C)
{
    save_trace_mark(&g_trace, SAVE_TRACE_LOCK_COMBAT,
                    SAVE_TRACE_ANSWER_UNKNOWN, "lockCombat");
    x86_guest_body(C, "XMen2.exe", 0x0049f860u);
}

static void x2_trace_004a6b50(CPU *C)
{
    save_trace_mark(&g_trace, SAVE_TRACE_EXTRACTION_SAVE_COMMAND,
                    SAVE_TRACE_ANSWER_UNKNOWN, "extractionPoint entry");
    x86_guest_body(C, "XMen2.exe", 0x004a6b50u);
}

static void x2_trace_005604f0(CPU *C)
{
    if (RD32(C->esp) == 0x004a6d01u)
        save_trace_mark(&g_trace, SAVE_TRACE_EXTRACTION_SAVE_COMMAND,
                        SAVE_TRACE_ANSWER_YES,
                        "queued saveloadProcess(4)");
    x86_guest_body(C, "XMen2.exe", 0x005604f0u);
}

static int path_is_main_engb(const char *path)
{
    static const char wanted[] = "ui/menus/main.engb";
    size_t i = 0;

    if (!path) return 0;
    while (path[0] == '.' && (path[1] == '/' || path[1] == '\\')) path += 2;
    for (; wanted[i] && path[i]; i++) {
        unsigned char ch = (unsigned char)path[i];
        if (ch == '\\') ch = '/';
        if (ch >= 'A' && ch <= 'Z') ch = (unsigned char)(ch + ('a' - 'A'));
        if (ch != (unsigned char)wanted[i]) return 0;
    }
    return wanted[i] == 0 && path[i] == 0;
}

void x2_save_trace_asset_open(const char *guest_path, int succeeded)
{
    if (path_is_main_engb(guest_path))
        save_trace_mark(&g_trace, SAVE_TRACE_MAIN_ENGB_OPEN,
                        succeeded ? SAVE_TRACE_ANSWER_YES
                                  : SAVE_TRACE_ANSWER_NO,
                        guest_path);
}

size_t x2_save_trace_runtime_report(char *out, size_t capacity)
{
    size_t required = 0;
    size_t trace_size;
    size_t autosave_size;
    size_t combined_size;
    size_t load_menu_size;

    if (save_trace_report(&g_trace, NULL, 0, &required)
            != SAVE_TRACE_REFUSED_CAPACITY || !required
        || !out || capacity < required)
        return 0;
    if (save_trace_report(&g_trace, out, capacity, NULL)
            != SAVE_TRACE_RECORDED)
        return 0;
    trace_size = required - 1u;
    autosave_size = x2_autosave_runtime_report(
        out + trace_size, capacity - trace_size);
    if (!autosave_size) return 0;
    combined_size = trace_size + autosave_size;
    load_menu_size = x2_load_game_menu_runtime_report(
        out + combined_size, capacity - combined_size);
    if (!load_menu_size) return 0;
    return combined_size + load_menu_size;
}

void x2_save_trace_runtime_print(void)
{
    char *report;
    size_t required = 0;

    if (save_trace_report(&g_trace, NULL, 0, &required)
            != SAVE_TRACE_REFUSED_CAPACITY || !required)
        return;
    report = (char *)malloc(required);
    if (!report) {
        fprintf(stderr, "save-trace: report allocation of %zu bytes failed\n",
                required);
        return;
    }
    if (save_trace_report(&g_trace, report, required, NULL)
            == SAVE_TRACE_RECORDED)
        fputs(report, stderr);
    free(report);
}

__attribute__((constructor))
static void x2_save_trace_register(void)
{
    const char *enabled = getenv("X2_SAVE_TRACE");

    g_trace_enabled = !(enabled && strcmp(enabled, "0") == 0);
    save_trace_init(&g_trace, g_trace_enabled);
    if (!g_trace_enabled) return;
    x86_register_override("XMen2.exe", 0x005c9970u, x2_trace_005c9970);
    x86_register_override("XMen2.exe", 0x0055fcd0u, x2_trace_0055fcd0);
    x86_register_override("XMen2.exe", 0x004aed10u, x2_trace_004aed10);
    x86_register_override("XMen2.exe", 0x0046e2b0u, x2_trace_0046e2b0);
    x86_register_override("XMen2.exe", 0x0049f140u, x2_trace_0049f140);
    x86_register_override("XMen2.exe", 0x004aeb80u, x2_trace_004aeb80);
    x86_register_override("XMen2.exe", 0x004ae990u, x2_trace_004ae990);
    x86_register_override("XMen2.exe", 0x004b15b0u, x2_trace_004b15b0);
    x86_register_override("XMen2.exe", 0x0046baf0u, x2_trace_0046baf0);
    x86_register_override("XMen2.exe", 0x0055fe70u, x2_trace_0055fe70);
    x86_register_override("XMen2.exe", 0x0049f860u, x2_trace_0049f860);
    x86_register_override("XMen2.exe", 0x004a6b50u, x2_trace_004a6b50);
    x86_register_override("XMen2.exe", 0x005604f0u, x2_trace_005604f0);
}
