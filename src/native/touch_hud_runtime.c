/* The retail CHud, and the one thing this port takes from it.
 *
 * `FUN_005a43d0` is CHud's single draw path, and the retail game reaches it
 * only through `FUN_005a62c0`, which tests the CHud visibility byte at +0x18
 * first. So this override running IS the game saying the gameplay HUD belongs
 * on screen this frame. That is published to the gameplay-control gate, which
 * is what keeps the touch overlay off the menus and the cutscenes.
 *
 * THE HUD IS NOT MOVED FROM HERE, and the reason is measured rather than
 * assumed. Shifting the authored igVec3f at this+8 does move part of the
 * retained tree -- the selector wheel and the health/energy bars follow it --
 * but the four character portraits do not, because they are placed from their
 * own coordinates rather than from that anchor. The result was the group
 * pulled apart on screen: portraits left behind, the X wheel and the bars
 * gone from between them. Two captures of the same scene, one with the shift
 * and one without, is what settled it; the version with the shift is the one
 * that looked broken.
 *
 * So the anchor is NOT the origin the HUD is laid out from, and relocating
 * the retail HUD to the requested corners needs per-element placement -- the
 * scene-graph structure under CHud, element by element. Until that is known,
 * this file moves nothing: a half-moved HUD is worse than an unmoved one, and
 * `touch_layout.c` already owns where each element is meant to end up.
 *
 * Two earlier defects are recorded here so they are not reintroduced:
 *
 *   - The per-character hook took its vector from `esp + 8`, the SECOND guest
 *     stack argument. The call is `FUN_005a3320(hero, &position, &colour, ...)`
 *     and that second argument is an igVec4f player colour -- (1,1,1,1) by
 *     default. So it never moved a panel: it wrote `width - 1.0f` into the red
 *     and blue channels of every character panel it drew.
 *   - Placement before that was a MIRROR of the anchor across both output
 *     axes. A reflection is not a position: it knows no element's size and
 *     lands wherever the arithmetic puts it.
 */
#include "touch_hud_runtime.h"

#include "../input/gameplay_control.h"
#include "guest_clock.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdio.h>
#include "guest_body.h"

enum { CHUD_DRAW = 0x005a43d0u };

/* Counted, because "the gate never saw a gameplay frame" and "the gate saw
 * them and still hid the overlay" are different failures, and a report that
 * cannot tell them apart cannot be debugged. */
static unsigned long g_root_calls;


static void x2_touch_hud_draw(CPU *C)
{
    g_root_calls++;
    x2_gameplay_control_hud_drawn(guest_clock_now_s());
    x86_guest_body(C, "XMen2.exe", 0x005a43d0u);
}

void x2_touch_hud_report(void)
{
    /* Reported at zero too: no gameplay HUD frame at all is a fact about the
       run -- it means the overlay could never have appeared -- and it is not
       the same as the run never having built this instrument. */
    fprintf(stderr,
            "TOUCH HUD: %lu gameplay HUD frame(s) published to the control "
            "gate; the retail HUD is drawn as authored (see the header for "
            "why it is not relocated). Last gate answer: %s.\n",
            g_root_calls,
            x2_gameplay_control_name(
                (int)x2_gameplay_control_state(guest_clock_now_s())));
}

__attribute__((constructor)) static void x2_touch_hud_register(void)
{
    x86_register_override("XMen2.exe", CHUD_DRAW, x2_touch_hud_draw);
}
