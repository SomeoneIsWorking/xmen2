/*
 * DOES THE PLAYER CONTROL A CHARACTER RIGHT NOW?
 *
 * The touch overlay and the relocated HUD must both answer this the same way,
 * so exactly one module answers it. Menus, cutscenes, loading and the front
 * end are all "no", and the answer must be observed rather than assumed --
 * a gate that guesses shows a movement stick over a save dialog.
 *
 * Two observations, and neither is a heuristic about what the screen looks
 * like:
 *
 *   - THE RETAIL GAME'S OWN HUD DECISION. CHud::draw (0x005a43d0) is reached
 *     only through 0x005a62c0, which tests the CHud visibility byte at +0x18
 *     first. So the game reaching our CHud::draw override IS the game saying
 *     the gameplay HUD belongs on screen this frame. It costs nothing and it
 *     cannot drift from the retail rule, because it *is* the retail rule.
 *
 *   - THE CUTSCENE CONTROL LOCK, from cutscene_player. A cinematic can hold
 *     input while the HUD stays up, so the HUD signal alone is not sufficient.
 *
 * The HUD signal is a heartbeat, not a level: it arrives once per drawn frame.
 * It expires when a whole frame is presented without it, so a menu turns the
 * answer to "no" on its first frame and a late frame changes nothing.
 */
#ifndef X2_GAMEPLAY_CONTROL_H
#define X2_GAMEPLAY_CONTROL_H

/* Why the answer is what it is. Reported, not inferred by the caller: "no
   overlay" and "no overlay because we have never seen a frame" are different
   facts about a run. */
typedef enum X2GameplayControl {
  kX2ControlNeverSeen = 0,  /* No HUD heartbeat has EVER arrived. */
  kX2ControlHudStale,       /* A frame was presented without it: a menu. */
  kX2ControlCutsceneLocked, /* HUD is up; a cinematic holds input. */
  kX2ControlActive,         /* The player drives a character. */
  kX2ControlCount
} X2GameplayControl;

const char *x2_gameplay_control_name(int state);

/* The retail HUD drew during `frame`, the frame in progress counted in
   presents (gpu_frames_presented). Called from the CHud::draw override. */
void x2_gameplay_control_hud_drawn(unsigned long frame);

/* The cutscene player's verdict this frame. Non-zero means a cinematic holds
   control. Sticky until the next call, like the lock it mirrors. */
void x2_gameplay_control_set_cutscene_locked(int locked);

/* The answer while `frame` is in progress. Never cached by callers. */
X2GameplayControl x2_gameplay_control_state(unsigned long frame);
int x2_gameplay_control_active(unsigned long frame);

/* Testing and the shutdown report: how many times each answer was given. */
void x2_gameplay_control_reset(void);
unsigned long x2_gameplay_control_answers(int state);

#endif
