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
 * causes are entirely different: the contacts never arrived, they arrived
 * while the overlay was hidden, they landed on no zone, or they reached the
 * pad and the guest was not reading it.
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
  unsigned long ignored_overlay_hidden;
  unsigned long zone_presses;
  unsigned long buttons_published;
  unsigned long buttons_refused;
  unsigned long axes_published;
  unsigned long axes_refused;
  unsigned long player_one_claimed;
  unsigned long player_one_refused;
  unsigned long cancellations;
} X2TouchCensus;

/* The live counts, for the runtime to add to. One set of numbers is reported
   and asserted, so the text and a test can never disagree about them. */
X2TouchCensus *x2_touch_census(void);

/* Copies them out. Null is ignored. */
void x2_touch_census_read(X2TouchCensus *out);

/* `has_window` is the only fact the runtime holds privately; the control
   mode, the source verdict and the gameplay gate are read from their own
   owners so this cannot report a second opinion about them. */
void x2_touch_census_report(int has_window);

#ifdef __cplusplus
}
#endif

#endif /* X2_TOUCH_CENSUS_H */
