/*
 * High-resolution extension for retail selected-row transforms.
 *
 * XMen2.exe FUN_005ea9e0 computes the selected row's Y/Z scale as
 * 1.336 - output_height * 0.0007, then calls FUN_005707d0 at 0x005ead96.
 * That is positive over the 2005 display range but becomes -0.176 at 2160p.
 * The row is still submitted; its scene-graph transform has collapsed before
 * D3D8 sees it. Keep the exact retail curve through the 800x600 UI reference,
 * then hold that reference share for larger outputs.
 */
#include "dialog_selection_scale.h"
#include "dialog_selection_scale_policy.h"

#include "d3d8_selector_probe.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    TITLE_TRANSFORM_BUILDER = 0x005707d0u,
    SELECTION_CALLER = 0x005ead9bu,
    TITLE_OUTPUT_HEIGHT = 0x00a0a000u
};

static unsigned long g_calls, g_selected, g_corrected, g_refused;

void fn_XMen2_005707d0(CPU *C);

static float stack_float(uint32_t address)
{
    uint32_t bits = RD32(address);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static void write_stack_float(uint32_t address, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    WR32(address, bits);
}

static void x2_dialog_selection_transform(CPU *C)
{
    uint32_t caller = RD32(C->esp);

    g_calls++;
    d3d8_selector_probe_title_builder_enter(C);
    if (caller == SELECTION_CALLER) {
        uint32_t height = RD32(TITLE_OUTPUT_HEIGHT);
        float supplied_y = stack_float(C->esp + 16u);
        float supplied_z = stack_float(C->esp + 20u);
        float retail = x2_dialog_selection_retail_scale(height);
        float extended = x2_dialog_selection_scale(height);

        g_selected++;
        if (height && fabsf(supplied_y - retail) < 0.00001f
                && fabsf(supplied_z - retail) < 0.00001f) {
            write_stack_float(C->esp + 16u, extended);
            write_stack_float(C->esp + 20u, extended);
            if (extended != supplied_y) g_corrected++;
        } else {
            g_refused++;
        }
    }
    fn_XMen2_005707d0(C);
    d3d8_selector_probe_title_builder_leave();
}

void x2_dialog_selection_scale_report(void)
{
    fprintf(stderr,
            "DIALOG SELECTION: %lu transform-builder call(s), %lu selected-row "
            "call(s), %lu high-resolution correction(s), %lu input "
            "mismatch(es) refused.\n",
            g_calls, g_selected, g_corrected, g_refused);
}

__attribute__((constructor))
static void x2_dialog_selection_scale_register(void)
{
    x86_register_override("XMen2.exe", TITLE_TRANSFORM_BUILDER,
                          x2_dialog_selection_transform);
}
