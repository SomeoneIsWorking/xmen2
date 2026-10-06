#ifndef HUD_GEOMETRY_H
#define HUD_GEOMETRY_H

/* Retail IGB scene-root bounds in the HUD's X/Z presentation plane.
 * tools/hud_geometry.py compares these values directly with the player's
 * assets. The cross uses its conservative root box, including its transform
 * envelope. */
#define HUD_HEALTH_FRAME_MIN_X (-61.0f)
#define HUD_HEALTH_FRAME_MAX_X (61.0f)
#define HUD_HEALTH_FRAME_MIN_Z (-15.0f)
#define HUD_HEALTH_FRAME_MAX_Z (15.0f)
#define HUD_CROSS_MIN_X (-40.56927490234375f)
#define HUD_CROSS_MAX_X (40.56928253173828f)
#define HUD_CROSS_MIN_Z (-40.704856872558594f)
#define HUD_CROSS_MAX_Z (40.56928253173828f)
#define HUD_PORTRAIT_EXTENT (14.717819213867188f)

#endif
