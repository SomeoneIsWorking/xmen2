#ifndef X2_DINPUT_PAD_REPORT_H
#define X2_DINPUT_PAD_REPORT_H

/*
 * What the pad path says about itself.
 *
 * Separate from the pad because it owns a different thing: the pad answers
 * the guest's polls, this decides what a run's totals mean and which of
 * several silences is the one in front of it. Every number here has a
 * denominator and every zero names its cause -- "nothing was pressed", "the
 * state was never refreshed" and "SDL never handed us a handle to read" are
 * three different defects that look identical in a row of zeroes.
 */

/* Everything one poll cycle can be counted doing. */
typedef struct X2PadPollCounts {
  unsigned long button_reads;
  unsigned long buttons_down;
  unsigned long buttons_unreadable;
  unsigned long buttons_no_pad;
  unsigned long axis_reads;
  unsigned long axes_off_centre;
  unsigned long refreshes;
} X2PadPollCounts;

/* The per-beat line: what the game asked the pad and what it got back. */
void dinput_pad_poll_report(void);

/* The end-of-run roll-call of connected devices. Printed once. */
void dinput_pad_report(void);

#endif /* X2_DINPUT_PAD_REPORT_H */
