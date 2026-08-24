#ifndef X2_SETTINGS_OVERLAY_STATE_H
#define X2_SETTINGS_OVERLAY_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

/* The pause-menu command and host event/render paths share this owner. */
void x2_settings_overlay_show(void);
void x2_settings_overlay_hide(void);
int x2_settings_overlay_visible(void);

#ifdef __cplusplus
}
#endif

#endif
