/* Touch-aware placement for the retail CHud scene graph.
 *
 * `FUN_005a43d0` is CHud's one draw path. It reads the authored igVec3f at
 * this+8 and derives every per-character call to `FUN_005a3320` from it, so
 * moving that root while the retained body runs moves the whole HUD and the
 * retail scene objects, drawing code and portrait click handler stay the sole
 * behavior owners.
 *
 * WHERE IT GOES IS NOT DECIDED HERE. `touch_layout.c` owns the rectangles, and
 * owns them for the touch controls too, because the previous design let this
 * file mirror the HUD to one place while `touch_controls.cpp` guessed
 * separately at where the mirror had landed -- two sources of truth for one
 * rectangle, agreeing only by luck.
 *
 * TWO DEFECTS THIS REPLACES, both silent:
 *
 *   - The per-character hook took its vector from `esp + 8`. Every override in
 *     this port reads its first guest stack argument at `esp + 4`; `esp + 8` is
 *     the SECOND. The call is `FUN_005a3320(hero, &position, &colour, ...)`,
 *     and that second argument is an igVec4f player colour -- (1,1,1,1) by
 *     default, per-player otherwise. So the hook never moved a panel: it wrote
 *     `width - 1.0f` into the red and blue channels of every character panel
 *     it drew, and restored them afterwards.
 *   - Placement was a MIRROR of the authored anchor across both output axes.
 *     A reflection is not a position: it knows no element's size and lands
 *     wherever the arithmetic puts it.
 */
#include "touch_hud_runtime.h"

#include "../input/gameplay_control.h"
#include "../input/touch_runtime.h"
#include "../presentation/touch_layout.h"
#include "guest_clock.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>

enum {
    CHUD_DRAW = 0x005a43d0u,
    CHUD_CHARACTER_DRAW = 0x005a3320u,
    CHUD_ROOT_OFFSET = 8u,
    VEC3_SIZE = 12u
};

/* Counted, because "the HUD did not move" and "the hook never ran" are
 * different failures and a report that cannot tell them apart cannot be
 * debugged. Every one of these is a denominator. */
static unsigned long g_root_calls;
static unsigned long g_character_calls;
static unsigned long g_relocated_roots;
static unsigned long g_relocated_characters;
static unsigned long g_no_layout;
static unsigned long g_unreadable;

/* The shift applied to the root this frame, so the per-character hook can
 * apply the SAME one. Thread-local and depth-scoped: it is only meaningful
 * inside a relocated root draw, and a character panel drawn outside one must
 * not be moved. */
static _Thread_local float g_shift_x;
static _Thread_local float g_shift_z;
static _Thread_local unsigned g_shift_active;

void fn_XMen2_005a43d0(CPU *C);
void fn_XMen2_005a3320(CPU *C);

/* The HUD's own space is output pixels with z downward -- CHud's authored
 * anchor is (48, 552) in an 800x600 output. There is no conversion to do, only
 * a read and a write of the two components that matter; the middle one is not
 * a screen axis and is left exactly as authored. */
static int read_point(uint32_t address, float *x, float *z)
{
    if (!address || address > UINT32_MAX - VEC3_SIZE ||
        !guest_memory_is_readable(address, VEC3_SIZE))
        return 0;
    *x = RDF32(address);
    *z = RDF32(address + 8u);
    return 1;
}

static void write_point(uint32_t address, float x, float z)
{
    WRF32(address, x);
    WRF32(address + 8u, z);
}

/* This frame's layout, or 0 when the viewport has none. */
static int current_layout(X2Rect *slots)
{
    X2LayoutViewport viewport;
    /* The runtime owns the viewport, so the drawn HUD and the touchable zones
       cannot disagree about how large the screen is. */
    if (!x2_touch_runtime_viewport(&viewport)) {
        return 0;
    }
    return x2_layout_build(viewport, slots);
}

static void x2_touch_hud_draw(CPU *C)
{
    const uint32_t root = C->ecx + CHUD_ROOT_OFFSET;
    X2Rect slots[kX2SlotCount];
    float x = 0.0f, z = 0.0f;
    int moved = 0;

    g_root_calls++;
    /* The game only reaches this override when it has decided the gameplay
       HUD belongs on screen (0x005a62c0 tests CHud+0x18 first), so this call
       IS the gameplay signal the overlay gate runs on. */
    x2_gameplay_control_hud_drawn(guest_clock_now_s());
    if (x2_touch_runtime_overlay_visible() && C->ecx &&
        C->ecx <= UINT32_MAX - CHUD_ROOT_OFFSET - VEC3_SIZE) {
        if (!current_layout(slots)) {
            g_no_layout++;
        } else if (!read_point(root, &x, &z)) {
            g_unreadable++;
        } else {
            /* A SHIFT, not an assignment. The authored anchor is the origin
               every element in the retained body is laid out from, so moving
               it by a delta preserves their relative arrangement; writing an
               absolute point would move the origin out from under a layout
               that was measured against it. */
            g_shift_x = slots[kX2SlotVitals].left - x;
            g_shift_z = slots[kX2SlotVitals].top - z;
            g_shift_active = 1;
            write_point(root, x + g_shift_x, z + g_shift_z);
            moved = 1;
            g_relocated_roots++;
        }
    }
    fn_XMen2_005a43d0(C);
    if (moved) {
        g_shift_active = 0;
        write_point(root, x, z);
    }
}

static void x2_touch_hud_character_draw(CPU *C)
{
    /* The POSITION is the first stack argument. The second is the player
       colour; see the header comment for what reading it did. */
    const uint32_t position = RD32(C->esp + 4u);
    float x = 0.0f, z = 0.0f;
    int moved = 0;

    g_character_calls++;
    if (g_shift_active) {
        if (!read_point(position, &x, &z)) {
            g_unreadable++;
        } else {
            write_point(position, x + g_shift_x, z + g_shift_z);
            moved = 1;
            g_relocated_characters++;
        }
    }
    fn_XMen2_005a3320(C);
    if (moved) write_point(position, x, z);
}

void x2_touch_hud_report(void)
{
    fprintf(stderr,
            "TOUCH HUD: %lu/%lu root draw(s) and %lu/%lu character draw(s) "
            "relocated; %lu without a layout, %lu unreadable vector(s).\n",
            g_relocated_roots, g_root_calls, g_relocated_characters,
            g_character_calls, g_no_layout, g_unreadable);
}

__attribute__((constructor)) static void x2_touch_hud_register(void)
{
    x86_register_override("XMen2.exe", CHUD_DRAW, x2_touch_hud_draw);
    x86_register_override("XMen2.exe", CHUD_CHARACTER_DRAW,
                          x2_touch_hud_character_draw);
}
