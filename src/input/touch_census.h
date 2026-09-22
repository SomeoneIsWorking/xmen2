#ifndef X2_TOUCH_CENSUS_H
#define X2_TOUCH_CENSUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* WHAT DID TOUCH ACTUALLY DO THIS RUN?
 *
 * Two platforms that ship this feature cannot run the host suite that proves
 * it: a phone and a browser. On both, every way it fails looks the same from
 * outside -- the player touches a control and nothing happens -- while the
 * causes are entirely different: the contacts never arrived, they landed on
 * no zone, they went to the retail pointer and the GUI did not want them, or
 * they reached the pad and the guest was not reading it.
 *
 * This owns those counts and the text made from them, apart from the runtime
 * that routes the contacts. A run in which nothing happened says WHICH
 * nothing it was rather than printing a row of zeroes.
 */
typedef struct X2TouchCensus {
  unsigned long contacts_down;
  unsigned long contacts_moved;
  unsigned long contacts_up;
  unsigned long contacts_canceled;
  unsigned long ignored_no_window;
  /*
   * WHERE A CONTACT WENT WHEN NO CONTROL WAS DRAWN.
   *
   * Outside gameplay -- the intro, the main menu, the pause and save screens
   * -- there is no overlay to press, and these contacts used to be counted
   * and thrown away. That is the whole of what a phone player met: a title
   * screen no finger could leave. They now become the retail GUI's own
   * pointer, and a refusal is a second finger arriving while the first still
   * holds retail's one mouse button.
   */
  unsigned long pointer_events;
  unsigned long pointer_refused;
  /*
   * THE PROMPTS THE RETAIL UI DRAWS AT THE FOOT OF ITS SCREENS.
   *
   * "Esc Back" names a key a phone does not have. In touch play the key is
   * taken off what is drawn and the remaining words become a control; a
   * contact inside one presses the key the prompt named. A refusal is the
   * keyboard injector saying so -- every slot held, or a binding that is not
   * a key -- and is the difference between "the button did nothing" and
   * "there was no button there".
   */
  unsigned long prompt_presses;
  unsigned long prompt_refused;
  unsigned long zone_presses;
  unsigned long buttons_published;
  unsigned long buttons_refused;
  unsigned long axes_published;
  unsigned long axes_refused;
  /* Whether the overlay has a pad to publish through at all. Zero of both
     means touch has never had anything to say; a refusal means the overlay
     is live and every press below is going nowhere. */
  unsigned long pad_attached;
  unsigned long pad_attach_refused;
  /*
   * WHY player one did or did not end up on the touch pad.
   *
   * These were one "claimed or refused" pair, and the report said "player one
   * already had a controller" whenever both were zero. That is three distinct
   * situations wearing one sentence: nothing was ever routed, a controller
   * really did own player one, or there was no pad slot to assign. A reader
   * chasing "touch does nothing" was sent to look for a controller that in
   * two of the three cases did not exist.
   */
  unsigned long player_one_claimed;
  unsigned long player_one_refused;
  unsigned long player_one_held_by_transient; /* a pad chosen this run */
  unsigned long player_one_held_by_setting;   /* a stored reservation */
  unsigned long player_one_no_slot;           /* the pad is not open yet */
  /*
   * Why a held zone was let go, counted apart.
   *
   * These were one number reported as "a lost window, rotation or layout
   * change" -- three causes the counter had never observed, and none of them
   * the one that was actually firing. The overlay's own pad was flipping the
   * input source away from touch, so every press cancelled itself a
   * millisecond after it was made.
   */
  unsigned long cancelled_overlay_hidden;
  unsigned long cancelled_window_gone;
  unsigned long cancelled_source_changed;
  unsigned long cancelled_window_changed;
} X2TouchCensus;

/* The live counts, for the runtime to add to. One set of numbers is reported
   and asserted, so the text and a test can never disagree about them. */
X2TouchCensus *x2_touch_census(void);

/* Copies them out. Null is ignored. */
void x2_touch_census_read(X2TouchCensus *out);

/* `has_window` is the only fact the runtime holds privately; the control
   mode, the source verdict and the gameplay gate are read from their own
   owners so this cannot report a second opinion about them. `tag` prefixes
   each line, so the live heartbeat and the shutdown roll-call are told apart.

   The heartbeat calls this from its own thread while the counts are being
   added to, which can tear a single number across a line. That is a
   diagnostic's risk and not a correctness one: the alternative is a lock on
   the input path for the benefit of a printout. */
/* `touch_devices` is what the host reported, or -1 if it was never asked:
   a run with no contacts means something different on a host with no
   touchscreen than on one with three. */
void x2_touch_census_report(const char *tag, int has_window, int touch_devices,
                            int touch_capable);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_CENSUS_H */
