#ifndef X2_BOOT_BLACKOUT_H
#define X2_BOOT_BLACKOUT_H

#include <stddef.h>

/* Presentation policy for boot-mode launches: a Menu or Continue boot has no
 * use for the retail boot's branding -- the legal-text loading backdrop and
 * the splash art -- so the frames between the boot dispatch and the
 * destination map's first completed load present black instead. The game
 * keeps running underneath; only what reaches the screen is withheld.
 *
 * The window is bounded twice over: it lifts on the boot load's own
 * successful map return, and it expires on its own (wall clock and presented
 * frames) with a printed line -- a blackout that never lifted would be a
 * black screen for the whole session, so the expiry may not be silent. */

void x2_boot_blackout_arm(const char *mode_name);
void x2_boot_blackout_disarm(const char *why);
int x2_boot_blackout_active(void);
/* Counted at present time by the renderer, so the report says how many
 * frames the player actually spent black. */
void x2_boot_blackout_frame_presented(void);
size_t x2_boot_blackout_report(char *out, size_t size);

#endif
