/*
 * What the layer says about itself at shutdown, and what it proves about
 * itself on demand.
 *
 * Kept apart from the interfaces on purpose: the report is what a reader sees
 * when nothing was drawn, and it has to be able to distinguish "the engine
 * never reached DirectX" from "it reached it and this host did nothing" from
 * "it drew and the frame was empty". Those three look identical on a black
 * screen and are three completely different bugs.
 */
#include "d3d8_host.h"
#include "d3d8_com.h"
#include "d3d8_device.h"
#include "d3d8_caps.h"
#include "d3d8_types.h"

#include <stdio.h>
#include <string.h>

void d3d8_host_report(void)
{
    if (!d3d8_host_enabled()) {
        printf("\nd3d8: the host Direct3D 8 was NOT enabled this run, so "
               "nothing here was exercised.\n");
        return;
    }
    printf("\n=== host Direct3D 8 ===\n");
    d3d8_device_report();
    d3d8_permissive_report();
    fflush(stdout);
}

/*
 * The caps block's own check.
 *
 * Not "does it look right" -- that cannot fail. It checks the two things that
 * would be silently wrong: that the struct this host writes is the size the
 * guest allocated for it (the game allocates 0xd4 and would be overrun by a
 * byte more), and that the fields land where a D3D8 caller reads them, tested
 * through the offsets rather than the field names.
 */
static int caps_selftest(void)
{
    D3DCAPS8 c;
    D3D8CapsLimits hw;
    const uint32_t *raw = (const uint32_t *)&c;
    int fails = 0;

    d3d8_caps_limits_default(&hw);
    memset(&c, 0xAA, sizeof c);
    d3d8_caps_fill(&c, 0, D3DDEVTYPE_HAL, &hw);

    if (sizeof c != 212) {
        fprintf(stderr, "  FAIL: D3DCAPS8 is %d bytes; the game allocates "
                        "212\n", (int)sizeof c);
        fails++;
    }
    /* DeviceType is dword 0 and MaxTextureWidth is dword 22; reading them
       positionally is what catches a field inserted in the wrong place. */
    if (raw[0] != D3DDEVTYPE_HAL) {
        fprintf(stderr, "  FAIL: dword 0 is 0x%08x, not the device type\n",
                raw[0]);
        fails++;
    }
    if (raw[22] != hw.max_texture_dim) {
        fprintf(stderr, "  FAIL: dword 22 is 0x%08x, not MaxTextureWidth "
                        "(%u)\n", raw[22], hw.max_texture_dim);
        fails++;
    }
    if (raw[49] != 0xFFFE0101u) {
        fprintf(stderr, "  FAIL: dword 49 is 0x%08x, not VertexShaderVersion "
                        "1.1\n", raw[49]);
        fails++;
    }
    /* Nothing may be left as the 0xAA fill: a field the filler forgot is a
       field the engine reads as garbage. memset(0) first would have hidden
       exactly that, which is why the fill is 0xAA. */
    {
        int i, untouched = 0;
        for (i = 0; i < (int)(sizeof c / 4); i++)
            if (raw[i] == 0xAAAAAAAAu) untouched++;
        if (untouched) {
            fprintf(stderr, "  FAIL: %d caps dword(s) were never written by "
                            "d3d8_caps_fill\n", untouched);
            fails++;
        }
    }
    printf("d3d8: caps self-test %s\n", fails ? "FAILED" : "passed");
    return fails;
}

int d3d8_host_selftest(void)
{
    int fails = 0;
    fails += d3d8_com_selftest();
    fails += caps_selftest();
    printf("d3d8: SELF-TEST %s -- %d failure(s)\n",
           fails ? "FAILED" : "PASSED", fails);
    fflush(stdout);
    return fails;
}
