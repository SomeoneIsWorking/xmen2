#ifndef X2_DISPLAY_MODE_SEED_H
#define X2_DISPLAY_MODE_SEED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Publication of the port's output size into the
   game's own persistent registry: HKCU\Software\Activision\X-Men Legends 2\
   Settings\Display "Resolution" as REG_SZ "%dx%d" -- byte for byte what the
   retail options screen writes (XMen2.exe formats it with the "%dx%d" at
   0x006a4b80 and registers the default "800x600" at 0x00619857). The engine
   parses that string itself and builds its D3D device from it, so this is
   how video.width/video.height reach game-space rendering. Publication runs
   once before guest startup and once at the retail settings-load boundary:
   a fresh profile's first-run branch writes its 800x600 default between those
   points, while a warm profile leaves the first publication intact.

   This is deliberately NOT the synthetic registry view rejected in issue
   #111 / claim C255: this does not synthesize a different value on guest
   reads. After publication the value belongs to the game exactly as if a
   player had set it in retail Options. The corollary, which is policy: a
   resolution chosen in the RETAIL options screen holds only until the next
   launch, when the port's own setting is published over it. */

/* Composed from boot control (startup.c) at the first guest call, ahead of
   the engine's settings registration; announces one line either way. */
void x2_display_mode_seed_boot(void);

/* Publish if -- and only if -- the stored value differs from video.width x
   video.height. Returns 1 when the store changed. */
int x2_display_mode_seed_publish(void);

/* The one authoritative retail Resolution encoding. Returns 1 only when
   dimensions are plausible and the complete "%ux%u" value fits. */
int x2_display_mode_seed_format(unsigned w, unsigned h, char *out_value,
                                int cap);

/* Whether the retail store currently contains the configured output mode.
   This distinguishes a no-op because the value already matched from a
   refused/failed publication. */
int x2_display_mode_seed_is_current(void);

/* The pure decision under publish(): 1 stores "%dx%d" of w/h into out_value
   because it differs from `stored` (NULL or empty means absent); 0 means no
   action -- stored already equals it, or the dimensions are not plausible
   output sizes. */
int x2_display_mode_seed_plan(const char *stored, unsigned w, unsigned h,
                              char *out_value, int cap);

/* The mode publish() last established as current, for the d3d8 adapter's
   enumeration. Zero before any successful publication or matching read. */
uint32_t x2_display_mode_seed_width(void);
uint32_t x2_display_mode_seed_height(void);

#ifdef __cplusplus
}
#endif

#endif /* X2_DISPLAY_MODE_SEED_H */
