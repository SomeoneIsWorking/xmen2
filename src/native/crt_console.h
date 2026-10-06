#ifndef X2_CRT_CONSOLE_H
#define X2_CRT_CONSOLE_H

#include <stddef.h>
#include <stdio.h>

/*
 * The guest's own standard output, routed to this port's log.
 *
 * WHY THIS EXISTS. The CRT shim wrote the guest's stdout and stderr to the
 * HOST's stdout, which is invisible wherever the host has no terminal -- in a
 * browser worker it goes nowhere at all. Measured (issue #158): the title's
 * allocation-failure handler prints
 *
 *     Allocation failure:
 *         Reason          = <one of five named reasons>
 *
 * through `libIGCore.dll!igOutput::igOutput::toStandardOut`, and then hangs on
 * purpose. The browser run hung exactly there and the message -- the title
 * naming its own cause -- was thrown away, so three separate investigations
 * localized the hang to threads, to the memory window, and to the renderer
 * before the address was read by hand.
 *
 * `printf` already logged; the FILE* family did not. This is the one owner for
 * both.
 *
 * LINES, not writes. The guest emits a message in several calls, so bytes are
 * assembled here and a line is logged when its newline arrives or when the
 * guest flushes. A partial line held at exit is logged too: the last thing a
 * program writes before it stops is the thing worth having.
 */

/* Is `f` the guest's stdout or stderr, rather than a file it opened? */
int crt_console_is(FILE *f);

/* Take `n` bytes the guest wrote to its console. Returns `n`; it cannot fail
   in a way the guest could act on, and dropping the text silently is the
   defect this replaces. */
size_t crt_console_write(const char *bytes, size_t n);

/* Log whatever line is being assembled, if any. */
void crt_console_flush(void);

#endif /* X2_CRT_CONSOLE_H */
