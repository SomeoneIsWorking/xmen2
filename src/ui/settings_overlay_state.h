#pragma once

namespace x2::ui {

/* The pause-menu command and host event/render paths share this owner. */
void settings_overlay_show(void);
void settings_overlay_hide(void);
int settings_overlay_visible(void);

/* F2, the host's own route to this overlay, handled here so every visibility
   transition has one owner. Returns 1 when the event toggled visibility and
   the caller must not show it to anything else -- not to RmlUi, not to the
   game. Only a fresh press counts; release and autorepeat return 0. */
int settings_overlay_toggle_key(int keycode, int is_down, int repeat);

} // namespace x2::ui
