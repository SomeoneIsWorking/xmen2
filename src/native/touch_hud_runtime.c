/* Touch-aware placement for the retail CHud scene graph.
 *
 * FUN_005a43d0 reads CHud's authored igVec3f at this+8 and passes it directly
 * to m_playercross's existing transform handler. It also derives each call to
 * FUN_005a3320 from that same root. Move the root while the retained body
 * runs, then remap only the per-character panel position back to the left.
 * The retail scene objects, drawing code, and portrait click handler remain
 * the sole behavior owners. */
#include "touch_hud_runtime.h"

#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include "../config/settings_store.h"
#include "../input/touch_runtime.h"
#include "../presentation/touch_hud_layout.h"

#include <stdio.h>

enum {
    CHUD_DRAW = 0x005a43d0u,
    CHUD_CHARACTER_DRAW = 0x005a3320u,
    CHUD_ROOT_OFFSET = 8u,
    VEC3_SIZE = 12u
};

static unsigned long g_root_calls;
static unsigned long g_character_calls;
static unsigned long g_relocated_roots;
static unsigned long g_relocated_characters;
static unsigned long g_refused;
static _Thread_local unsigned g_layout_depth;

void fn_XMen2_005a43d0(CPU *C);
void fn_XMen2_005a3320(CPU *C);

static void write_point(uint32_t address, X2TouchHudPoint point)
{
    WRF32(address, point.x);
    WRF32(address + 8u, point.z);
}

static void x2_touch_hud_draw(CPU *C)
{
    const X2Settings *settings = x2_settings_store();
    const uint32_t root = C->ecx + CHUD_ROOT_OFFSET;
    X2TouchHudPoint original;
    X2TouchHudPoint relocated;
    int changed = 0;

    g_root_calls++;
    if (x2_touch_runtime_overlay_visible()) {
        if (C->ecx <= UINT32_MAX - CHUD_ROOT_OFFSET - VEC3_SIZE &&
            C->ecx && guest_memory_is_readable(root, VEC3_SIZE)) {
            original.x = RDF32(root);
            original.z = RDF32(root + 8u);
            if (x2_touch_hud_root_to_top_right(
                    original, settings->width, settings->height, &relocated)) {
                write_point(root, relocated);
                changed = 1;
                g_relocated_roots++;
            } else {
                g_refused++;
            }
        } else {
            g_refused++;
        }
    }
    if (changed) g_layout_depth++;
    fn_XMen2_005a43d0(C);
    if (changed) {
        g_layout_depth--;
        write_point(root, original);
    }
}

static void x2_touch_hud_character_draw(CPU *C)
{
    const X2Settings *settings = x2_settings_store();
    const uint32_t position = RD32(C->esp + 8u);
    X2TouchHudPoint original;
    X2TouchHudPoint relocated;
    int changed = 0;

    g_character_calls++;
    if (g_layout_depth) {
        if (position <= UINT32_MAX - VEC3_SIZE && position &&
            guest_memory_is_readable(position, VEC3_SIZE)) {
            original.x = RDF32(position);
            original.z = RDF32(position + 8u);
            if (x2_touch_hud_panel_to_top_left(
                    original, settings->width, &relocated)) {
                write_point(position, relocated);
                changed = 1;
                g_relocated_characters++;
            } else {
                g_refused++;
            }
        } else {
            g_refused++;
        }
    }
    fn_XMen2_005a3320(C);
    if (changed) write_point(position, original);
}

void x2_touch_hud_report(void)
{
    fprintf(stderr,
            "TOUCH HUD: %lu/%lu root draw(s) and %lu/%lu character draw(s) "
            "relocated; %lu invalid transform(s) refused.\n",
            g_relocated_roots, g_root_calls,
            g_relocated_characters, g_character_calls, g_refused);
}

__attribute__((constructor))
static void x2_touch_hud_register(void)
{
    x86_register_override("XMen2.exe", CHUD_DRAW, x2_touch_hud_draw);
    x86_register_override("XMen2.exe", CHUD_CHARACTER_DRAW,
                          x2_touch_hud_character_draw);
}
