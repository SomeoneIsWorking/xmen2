#ifndef X2_SCRIPT_TRACE_H
#define X2_SCRIPT_TRACE_H

/*
 * Which BehavEd scripts a run actually ran.
 *
 * A level is driven by its scripts -- tutorial1.py locks the controls and
 * starts the opening conversation, and a chain of four more scripts is what
 * eventually unlocks them again. When that chain stops, the game keeps
 * rendering and simply never hands control back, which from outside is
 * indistinguishable from a hang. The only way to tell WHERE it stopped is to
 * know which scripts ran, so this records every launch by name.
 */
void script_trace_report(void);

#endif
