/*
 * Run the guest's own body for a function this port also implements natively.
 *
 * Every native override needs a way to say "not this time -- do what the game
 * did". Guest code lives in the player's mapped image, so the call is named by
 * module and linked entry point, resolved after relocation, and executed by
 * x86port's ordinary runtime path.
 *
 * It goes STRAIGHT to the execution engine rather than through the dispatcher,
 * because the dispatcher would find the override registered at that address and
 * call it again. An override asking for the original body is the one caller
 * that must not be dispatched.
 */
#ifndef GUEST_BODY_H
#define GUEST_BODY_H

#include "x86rt.h"
#include <stdint.h>

/* `module` is the image's file name as the loader knows it ("XMen2.exe"), and
   `linked_ep` the entry point at that module's PREFERRED base -- the address
   the disassembly shows, which is stable across runs while the mapped one is
   not. Stops the run rather than returning if it cannot resolve or execute:
   an override that silently skips the original body leaves the guest in a
   state nothing downstream can explain. */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep);

/* Continue a guest body at `linked_at`, an address inside it, after an
   override has built the frame the body has there; the body's return address
   is the word at `frame_esp`. For an override that replaces only a leading
   part of a retail function. Stops the run on failure, as x86_guest_body. */
void x86_guest_body_resume(CPU *C, const char *module, uint32_t linked_at,
                           uint32_t frame_esp);

/* The same call for code that must REPORT a failure rather than stop: returns
   1 when the body ran, 0 with the reason in `why` when the module is not
   mapped or the engine declined it. For the self-test battery, whose whole job
   is to report; production overrides use x86_guest_body, because there a
   skipped body is not a result. */
int x86_guest_body_try(CPU *C, const char *module, uint32_t linked_ep,
                       char *why, unsigned why_len);

#endif /* GUEST_BODY_H */
