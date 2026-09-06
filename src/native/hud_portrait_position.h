#ifndef X2_HUD_PORTRAIT_POSITION_H
#define X2_HUD_PORTRAIT_POSITION_H

#include <stdint.h>

/* CHudPortrait's single-player parent/local transform (XMen2.exe 005a1650).
 * xyz uses the game's x/depth/z convention, not window x/y. */
typedef struct {
  float xyz[3];
  float scale;
} X2HudPortraitAnchor;

void x2_hud_portrait_position(const X2HudPortraitAnchor *parent,
                              const X2HudPortraitAnchor *local,
                              float output[3]);

/* The mapper receives the authored output after native/original verification.
 * It must preserve output[1], which is scene depth. NULL restores retail
 * layout. */
typedef void (*X2HudPortraitMap)(void *context, uint32_t portrait,
                                 float output[3]);
void x2_hud_portrait_position_mapper(X2HudPortraitMap mapper, void *context);
void x2_hud_portrait_position_report(void);

#endif
