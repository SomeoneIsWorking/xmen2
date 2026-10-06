#ifndef X2_HOST_TOUCH_H
#define X2_HOST_TOUCH_H

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
#ifdef __cplusplus
extern "C" {
#endif

/* Nonzero when a touchscreen exists or the host says one can be used. */
int x2_host_touch_capable(void);

/* What SDL's device list says right now. Reported beside the capability so a
   run with neither can be told from a run this was never asked in. */
int x2_host_touch_devices(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_HOST_TOUCH_H */
