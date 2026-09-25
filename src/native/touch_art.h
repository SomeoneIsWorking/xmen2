/* The retail art files the touch overlay draws its controls from. */
#ifndef X2_TOUCH_ART_H
#define X2_TOUCH_ART_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum X2TouchArt {
  /* Textures/ui/talent_icons.IGB: a 4x4 grid of round icons in the power
     icons' own style -- fist, open hand, wing among them. */
  X2_TOUCH_ART_TALENTS = 0,
  /* Textures/ui/hud.IGB: the HUD atlas, a 4x8 grid of 32-pixel cells --
     the potions and a screen frame among them. */
  X2_TOUCH_ART_HUD,
  X2_TOUCH_ART_COUNT
} X2TouchArt;

/* The host path of that file in the user's install; never null. */
const char *x2_touch_art_path(X2TouchArt art);

#ifdef __cplusplus
}
#endif

#endif
