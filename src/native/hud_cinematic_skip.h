#pragma once

/* The retail scripted cinematic (`cinematicStart`, a mission briefing): CHud
   update 0x0059f1a0 runs its cancel script when action 20 is down. See
   docs/RE/cutscene_player.md. */

namespace x2::native {

/* A cinematic is shown and names a cancel script, so action 20 cancels it. */
bool hud_cinematic_skippable();

/* Action 20 down for the next CHud update only: the press Escape makes. */
void hud_cinematic_skip_press();

struct HudCinematicSkipSnapshot {
  bool shown;
  char cancel_script[0x80];
  unsigned long presses;
  unsigned long forced;
};
void hud_cinematic_skip_snapshot(HudCinematicSkipSnapshot *out);

} // namespace x2::native
