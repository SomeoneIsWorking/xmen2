#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>

#include "shadow_setting.h"
#include "shadow_trace.h"

typedef LSTATUS (WINAPI *reg_query_value_ex_a_fn)(
    HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);

static reg_query_value_ex_a_fn g_real_reg_query_value_ex_a;
static int g_forced_value = -1;
static int g_original_value = -1;
static unsigned g_forced_reads;

static LSTATUS WINAPI forced_RegQueryValueExA(
    HKEY key, LPCSTR value_name, LPDWORD reserved, LPDWORD type,
    LPBYTE data, LPDWORD size)
{
    DWORD capacity = size ? *size : 0;
    LSTATUS status = g_real_reg_query_value_ex_a(
        key, value_name, reserved, type, data, size);
    if (status == ERROR_SUCCESS && value_name
            && strcmp(value_name, "DetailedShadow") == 0
            && data && size && capacity >= sizeof(DWORD)
            && *size >= sizeof(DWORD) && (!type || *type == REG_DWORD)) {
        if (g_original_value < 0) g_original_value = (int)*(DWORD *)data;
        *(DWORD *)data = (DWORD)g_forced_value;
        *size = sizeof(DWORD);
        if (type) *type = REG_DWORD;
        g_forced_reads++;
    }
    return status;
}

static IMAGE_THUNK_DATA *find_query_iat_slot(unsigned char *base)
{
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    IMAGE_NT_HEADERS *nt;
    IMAGE_IMPORT_DESCRIPTOR *descriptor;
    DWORD import_rva;

    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return NULL;
    import_rva = nt->OptionalHeader.DataDirectory[
        IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress;
    if (!import_rva) return NULL;
    descriptor = (IMAGE_IMPORT_DESCRIPTOR *)(base + import_rva);
    for (; descriptor->Name; descriptor++) {
        IMAGE_THUNK_DATA *names;
        IMAGE_THUNK_DATA *iat;
        const char *module_name = (const char *)(base + descriptor->Name);
        if (_stricmp(module_name, "ADVAPI32.dll") != 0) continue;
        if (!descriptor->OriginalFirstThunk || !descriptor->FirstThunk)
            return NULL;
        names = (IMAGE_THUNK_DATA *)(base + descriptor->OriginalFirstThunk);
        iat = (IMAGE_THUNK_DATA *)(base + descriptor->FirstThunk);
        for (; names->u1.AddressOfData; names++, iat++) {
            IMAGE_IMPORT_BY_NAME *name;
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            name = (IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData);
            if (strcmp((const char *)name->Name, "RegQueryValueExA") == 0)
                return iat;
        }
        return NULL;
    }
    return NULL;
}

int shadow_setting_install_query_override(int forced_value)
{
    HMODULE executable = GetModuleHandleA(NULL);
    HMODULE advapi = GetModuleHandleA("ADVAPI32.dll");
    IMAGE_THUNK_DATA *slot;
    FARPROC resolved;
    DWORD old_protection;

    if (g_real_reg_query_value_ex_a)
        return forced_value == g_forced_value;
    if ((forced_value != 0 && forced_value != 1) || !executable || !advapi)
        return 0;
    slot = find_query_iat_slot((unsigned char *)executable);
    resolved = GetProcAddress(advapi, "RegQueryValueExA");
    if (!slot || !resolved
            || (FARPROC)(UINT_PTR)slot->u1.Function != resolved)
        return 0;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old_protection))
        return 0;
    _Static_assert(sizeof g_real_reg_query_value_ex_a == sizeof resolved,
                   "Win32 data and function pointers must have equal size");
    memcpy(&g_real_reg_query_value_ex_a, &resolved, sizeof resolved);
    g_forced_value = forced_value;
    slot->u1.Function = (DWORD)(UINT_PTR)forced_RegQueryValueExA;
    if (!VirtualProtect(slot, sizeof *slot, old_protection,
                        &old_protection)) {
        slot->u1.Function = (DWORD)(UINT_PTR)resolved;
        g_real_reg_query_value_ex_a = NULL;
        g_forced_value = -1;
        return 0;
    }
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof *slot);
    return 1;
}

unsigned shadow_setting_forced_reads(void)
{
    return g_forced_reads;
}

int shadow_setting_original_value(void)
{
    return g_original_value;
}

/*
 * Locate the value XMen2.exe loaded for Settings\Display\DetailedShadow.
 *
 * RVA 0x668d40 is not trusted on its own. Two independently exported settings
 * loaders store AL there with the five bytes A2 40 8D A6 00; the instruction
 * at RVA 0x2198c3 is checked before the value is exposed. Every referenced
 * byte must belong to committed memory allocated with the executable image.
 */
unsigned char *shadow_setting_address(void)
{
    enum {
        STORE_RVA = 0x2198c3u,
        STORE_SIZE = 5,
        VALUE_RVA = 0x668d40u
    };
    static int checked, valid;
    MEMORY_BASIC_INFORMATION start_region, end_region, value_region;
    unsigned char *base = (unsigned char *)GetModuleHandleA(NULL);

    if (!base) return NULL;
    if (!checked) {
        checked = 1;
        valid = VirtualQuery(base + STORE_RVA, &start_region,
                             sizeof start_region) == sizeof start_region
             && start_region.State == MEM_COMMIT
             && start_region.AllocationBase == base
             && VirtualQuery(base + STORE_RVA + STORE_SIZE - 1u, &end_region,
                             sizeof end_region) == sizeof end_region
             && end_region.State == MEM_COMMIT
             && end_region.AllocationBase == base
             && VirtualQuery(base + VALUE_RVA, &value_region,
                             sizeof value_region) == sizeof value_region
             && value_region.State == MEM_COMMIT
             && value_region.AllocationBase == base
             && shadow_trace_setting_anchor_matches(base + STORE_RVA,
                                                     STORE_SIZE);
    }
    return valid ? base + VALUE_RVA : NULL;
}
