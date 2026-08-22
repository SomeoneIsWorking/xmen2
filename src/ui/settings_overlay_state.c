#include "settings_overlay_state.h"

static int g_visible;

void x2_settings_overlay_show(void)
{
    g_visible = 1;
}

void x2_settings_overlay_hide(void)
{
    g_visible = 0;
}

void x2_settings_overlay_toggle(void)
{
    g_visible = !g_visible;
}

int x2_settings_overlay_visible(void)
{
    return g_visible;
}
