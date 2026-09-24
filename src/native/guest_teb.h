#ifndef X2_GUEST_TEB_H
#define X2_GUEST_TEB_H

/*
 * A guest thread's TEB (the block FS addresses) and what hangs off it.
 *
 * FS:[0] is the head of the SEH exception-registration chain, which MSVC's
 * try/except prologues push and pop; it needs one real word per thread,
 * holding Win32's end-of-chain sentinel. Exception DELIVERY is not modelled:
 * nothing walks that chain, and a guest raise expecting it to would find so.
 *
 * Win32 implicit TLS: the `__declspec(thread)` data a PE image declares in its
 * TLS directory, which the Windows loader gives every thread a private copy
 * of. Code reaches it as `fs:[0x2c]` (the TEB's ThreadLocalStoragePointer),
 * then `[array + index * 4]`, then an offset into the image's block. A TEB
 * whose +0x2c is zero turns the first such access into a read of address 0.
 *
 * XMen2.exe is the one retail image with a TLS directory (64 bytes, no
 * callbacks); GameSpy's per-thread state lives in it and is first touched when
 * the Play Online menu starts networking.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map the main thread's TEB at its fixed low address, give it the SEH
   sentinel and its static TLS, and return its base for FS; 0, having said
   why, when it cannot. Call after every image is registered. */
uint32_t guest_teb_main_init(void);

/* Record a mapped image's TLS template and write its index where the image
   reads it. 1 when the image has none or it was recorded; 0, having said
   why, for one this host cannot honour (a TLS callback, too many images). */
int guest_teb_register_image(uint32_t base);

/* Give the thread whose TEB is at `tib` its own copy of every registered
   template and point TEB+0x2c at them. 0, having said why, when the guest
   heap is short; the TEB is then left without TLS. */
int guest_teb_tls_attach(uint32_t tib);

/* Return what guest_teb_tls_attach gave `tib`; safe on a TEB that has none. */
void guest_teb_tls_detach(uint32_t tib);

#ifdef __cplusplus
}
#endif

#endif /* X2_GUEST_TEB_H */
