#ifndef X2_TOUCH_PAD_H
#define X2_TOUCH_PAD_H

/*
 * The synthetic gamepad the on-screen controls publish through, and who owns
 * player one while they do.
 *
 * Separate from the touch runtime because it owns a different thing: the
 * runtime turns contacts into actions, this owns a device's existence, its
 * timing against the guest's one enumeration, and the player slot without
 * which the guest never reads it. Both halves were defects that looked like a
 * working overlay.
 */
namespace x2::input::touch_pad {

/*
 * Attach the pad now, while the guest can still see it, if this host can
 * produce touch at all or the setting forces the controls on.
 *
 * Called when the window arrives, which is before the guest enumerates game
 * controllers. A pad attached later is one it will never poll.
 */
void prepare_for_host();

/* Attach the pad if it is not attached; counted, reported once on failure. */
void ensure();

/* Fill player one's vacancy with the pad, if a real controller has not
   already claimed it. Retried until a slot exists. */
void claim_player_one();

/* Touch devices this host reported when asked, or -1 if it never was. */
int host_devices();

/* What the capability probe answered when the window arrived, or -1 if it was
   never asked. */
int host_capable();

} // namespace x2::input::touch_pad

#endif /* X2_TOUCH_PAD_H */
