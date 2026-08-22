#ifndef X2_SETTINGS_OVERLAY_STATE_H
#define X2_SETTINGS_OVERLAY_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* Shared visibility state for every way the settings overlay can be opened.
   The retail menu bridge and the SDL/RmlUi event path deliberately use this
   same owner, so opening from a guest command cannot drift from F1 capture. */
void x2_settings_overlay_show(void);
void x2_settings_overlay_hide(void);
void x2_settings_overlay_toggle(void);
int x2_settings_overlay_visible(void);

#ifdef __cplusplus
}
#endif

#endif
