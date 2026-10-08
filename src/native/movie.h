#pragma once

/*
 * The libCriMovie guest ABI bridge's reports.
 *
 * The bridge itself is installed through the override table; what a caller
 * outside it needs is what the movie owner knows, and that is these two.
 */

namespace x2::native {

/* The end-of-run roll-call: the decoder's own report and the frame probe. */
void movie_report(void);

/*
 * The movie line on the beat: how many times the guest asked for each entry
 * point, and what the loaded movie is doing.
 *
 * A title waits for its cutscene to report itself finished and does not time
 * that wait out, so a movie that stops advancing stops the product. These
 * numbers say which side stopped -- the guest asking or this port answering.
 */
void movie_beat_report(void);

} // namespace x2::native
