/*
 * CAN THE CINEMATIC ON SCREEN BE SKIPPED, AND HAS A FINGER ASKED?
 *
 * A keyboard skips an authored cutscene with Escape: the cutscene player
 * (src/native/cutscene_player.c) sees the retail skip action go down and runs
 * the owned sequence to its release. A phone has no Escape, so touch play
 * offers a Skip button -- and it must appear exactly while that route can
 * run, and pressing it must take exactly that route.
 *
 * So this owner holds two facts and no behaviour. The cutscene player OFFERS a
 * skip every input poll (a sequence it owns holds the player's controls), and
 * TAKES a request on the same poll, performing the one skip it already
 * performs for the key. The touch layer only READS the offer and REQUESTS.
 * Neither side reaches into the other, and there is no second skip path.
 *
 * The poll and the touch events run on different threads, so both facts are
 * atomic. A request made while nothing is offered is refused rather than
 * held: a tap that lands after the cutscene ended must not skip the next one.
 */
#ifndef X2_CUTSCENE_SKIP_H
#define X2_CUTSCENE_SKIP_H

#ifdef __cplusplus
extern "C" {
#endif

/* The cutscene player, once per input poll: can a skip run now? Withdrawing
   the offer also drops a request nobody took. */
void x2_cutscene_skip_offer(int available);

/* The touch layer: is a skip offered now? */
int x2_cutscene_skip_available(void);

/* The touch layer: ask for the offered skip. Returns 1 when it was accepted,
   0 when nothing was offered. */
int x2_cutscene_skip_request(void);

/* The cutscene player: take a pending request, once. */
int x2_cutscene_skip_take_request(void);

/* Denominators: requests accepted, refused, and taken by the player. */
typedef struct X2CutsceneSkipCounts {
  unsigned long accepted;
  unsigned long refused;
  unsigned long taken;
} X2CutsceneSkipCounts;
X2CutsceneSkipCounts x2_cutscene_skip_counts(void);

/* Testing: forget everything. */
void x2_cutscene_skip_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_CUTSCENE_SKIP_H */
