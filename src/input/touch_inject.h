#pragma once

#include <cstdint>

namespace x2::input {

/* Contact phases, for the injector below. They are the SDL finger events by
   another name, kept separate so a caller needs no SDL headers. */
enum class TouchPhase : int { Down, Motion, Up, Cancel };

/*
 * Drive a contact as if the host had reported one, at a position normalized
 * to the window.
 *
 * A machine with no touchscreen cannot press an on-screen control, and a run
 * driven by a script written before it started answers whatever screen it
 * drifted onto. This goes through the SAME note-source and routing calls the
 * real host event takes, so what it exercises is the shipping path and not a
 * second copy of it. Returns what the routing returned.
 */
int touch_inject(int64_t contact_id, float x, float y, TouchPhase phase);

} // namespace x2::input
