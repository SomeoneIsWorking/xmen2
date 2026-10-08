#pragma once

/*
 * Can this host produce touch at all, before any finger has landed?
 *
 * Asked once, when the window arrives, because the answer decides whether the
 * on-screen controls' pad is attached in time for the guest's single
 * enumeration. "Has a finger landed" is a different question and is owned by
 * touch_source; by the time it can be answered the enumeration is over.
 *
 * The two answers differ by host and that is the whole reason this seam
 * exists: a desktop or phone enumerates its touchscreens and SDL lists them,
 * while a browser lists nothing until a touch has already happened and
 * answers the capability question through the platform instead. The policy
 * above it stays platform-neutral.
 */

namespace x2::native {

/* Nonzero when a touchscreen exists or the host says one can be used. */
int host_touch_capable();

/* What SDL's device list says right now. Reported beside the capability so a
   run with neither can be told from a run this was never asked in. */
int host_touch_devices();

} // namespace x2::native
