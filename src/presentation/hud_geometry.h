#pragma once

namespace x2::presentation {

/* Retail IGB scene-root bounds in the HUD's X/Z presentation plane.
 * tools/hud_geometry.py compares these values directly with the player's
 * assets. The cross uses its conservative root box, including its transform
 * envelope. */
inline constexpr float HUD_HEALTH_FRAME_MIN_X = -61.0f;
inline constexpr float HUD_HEALTH_FRAME_MAX_X = 61.0f;
inline constexpr float HUD_HEALTH_FRAME_MIN_Z = -15.0f;
inline constexpr float HUD_HEALTH_FRAME_MAX_Z = 15.0f;
inline constexpr float HUD_CROSS_MIN_X = -40.56927490234375f;
inline constexpr float HUD_CROSS_MAX_X = 40.56928253173828f;
inline constexpr float HUD_CROSS_MIN_Z = -40.704856872558594f;
inline constexpr float HUD_CROSS_MAX_Z = 40.56928253173828f;
inline constexpr float HUD_PORTRAIT_EXTENT = 14.717819213867188f;

} // namespace x2::presentation
