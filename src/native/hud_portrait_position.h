#pragma once

#include <cstdint>

namespace x2::native {

/* CHudPortrait's single-player parent/local transform (XMen2.exe 005a1650).
 * xyz uses the game's x/depth/z convention, not window x/y. */
struct HudPortraitAnchor {
  float xyz[3];
  float scale;
};

void hud_portrait_position(const HudPortraitAnchor *parent,
                           const HudPortraitAnchor *local, float output[3]);

/* The mapper receives the authored output after native/original verification.
 * It must preserve output[1], which is scene depth. NULL restores retail
 * layout. */
using HudPortraitMap = void (*)(void *context, uint32_t portrait,
                                float output[3]);
void hud_portrait_position_mapper(HudPortraitMap mapper, void *context);
void hud_portrait_position_report(void);

} // namespace x2::native
