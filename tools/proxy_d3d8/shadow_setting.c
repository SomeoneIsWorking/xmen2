#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "shadow_setting.h"
#include "shadow_trace.h"

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
