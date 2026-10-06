/*
 * The hot-guest-body probe. See x86_hotep.c.
 *
 * Unarmed it costs one predictable branch per dispatch and answers nothing --
 * deliberately: a probe that never counted must not print a plausible zero.
 */
#ifndef X86_HOTEP_H
#define X86_HOTEP_H

#include <stdint.h>

/* X2_HOTEP=<n>: track the top n entry points. NULL or 0 leaves it unarmed. */
void x86_hotep_arm(const char *arg);

/* Whether arming happened, for callers that must not pay to measure a span. */
int x86_hotep_armed(void);

/* One dispatch of `ep` that took `ns` nanoseconds, exclusive of its callees. */
void x86_hotep_count(uint32_t ep, unsigned long long ns);

/* The top `cap` entry points by time since the last read; 0 when unarmed. */
unsigned int x86_hotep_sorted(uint32_t *ep, unsigned long long *ns,
                              unsigned long *hits, unsigned int cap);

/* Distinct entry points that hashed onto an occupied slot, capped at 16: any
   non-zero value means the table stopped taking new keys. */
unsigned int x86_hotep_collisions(void);

#endif
