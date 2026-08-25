/* F2 toggle policy for the Port Settings overlay, against its shipping owner.
   The keycode constant in settings_overlay_state.c is deliberately an integer
   literal so that owner needs no SDL headers; this test is the code-to-truth
   check that the literal still means SDL's F2. */
#include "settings_overlay_state.h"

#include <SDL3/SDL.h>
#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

int main(void)
{
    _Static_assert(SDLK_F2 == 0x4000003bu,
                   "the owner's literal must equal SDL's SDLK_F2");

    CHECK(!x2_settings_overlay_visible());

    /* A fresh press shows it; the matching release is not a second toggle. */
    CHECK(x2_settings_overlay_toggle_key((int)SDLK_F2, 1, 0) == 1);
    CHECK(x2_settings_overlay_visible());
    CHECK(x2_settings_overlay_toggle_key((int)SDLK_F2, 0, 0) == 0);
    CHECK(x2_settings_overlay_visible());

    /* Autorepeat must not flicker the overlay while the key is held. */
    CHECK(x2_settings_overlay_toggle_key((int)SDLK_F2, 1, 1) == 0);
    CHECK(x2_settings_overlay_visible());

    /* Any other key, pressed or released, belongs to someone else. */
    CHECK(x2_settings_overlay_toggle_key((int)SDLK_ESCAPE, 1, 0) == 0);
    CHECK(x2_settings_overlay_toggle_key((int)'a', 1, 0) == 0);
    CHECK(x2_settings_overlay_toggle_key(0, 1, 0) == 0);
    CHECK(x2_settings_overlay_visible());

    /* Second press hides again. */
    CHECK(x2_settings_overlay_toggle_key((int)SDLK_F2, 1, 0) == 1);
    CHECK(!x2_settings_overlay_visible());
    CHECK(x2_settings_overlay_toggle_key((int)SDLK_F2, 1, 1) == 0);
    CHECK(!x2_settings_overlay_visible());

    printf("test_settings_overlay: %d F2 toggle checks passed\n", checks);
    return 0;
}
