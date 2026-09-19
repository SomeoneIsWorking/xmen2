#ifndef X2_GUEST_INSPECT_H
#define X2_GUEST_INSPECT_H

#include <stdint.h>

/*
 * Read-only descriptions of guest memory, for diagnostics.
 *
 * WHY THESE PRINT EVERYTHING. A report that shows only the words it could
 * explain cannot be told from a report that ran against nothing: measured in
 * issue #158, a stack scan that printed only the words landing in a mapped
 * image named the failing FUNCTION and threw away the object the failing size
 * came from, because a heap pointer lands in no image. Both of these print
 * every word they were asked for, annotate the ones they can, and say so
 * plainly when the address is not readable at all.
 *
 * Neither touches guest state: a diagnostic that can move the run it is
 * describing is worse than no diagnostic.
 */

/*
 * The `words` words at `esp`, eight to a line, with a second pass naming each
 * word that is a return address into a mapped image or a pointer to something
 * whose first word is one -- which is what a C++ object with a vtable looks
 * like, and is how the type behind a failure gets named.
 */
void guest_inspect_stack(uint32_t esp, unsigned words, const char *tag);

/* The `words` words at `address`, eight to a line, for following a pointer a
   previous report named. Refuses by saying the address is not readable. */
void guest_inspect_words(uint32_t address, unsigned words, const char *tag);

#endif
