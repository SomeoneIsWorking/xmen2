/*
 * Run the guest's own body for a function this port also implements natively.
 *
 * Every native override needs a way to say "not this time -- do what the game
 * did". That used to be a direct call to the translated body's C symbol, which
 * only worked because the whole corpus was linked in. With guest code executed
 * from the player's own images there is no symbol to call: the body is a range
 * of guest bytes at an address that depends on where the module landed, so the
 * call is made by (module, linked entry point) and resolved at run time.
 *
 * It goes STRAIGHT to the execution engine rather than through the dispatcher,
 * because the dispatcher would find the override registered at that address and
 * call it again. An override asking for the original body is the one caller
 * that must not be dispatched.
 */
#ifndef GUEST_BODY_H
#define GUEST_BODY_H

#include <stdint.h>
#include "x86rt.h"

/* `module` is the image's file name as the loader knows it ("XMen2.exe"), and
   `linked_ep` the entry point at that module's PREFERRED base -- the address
   the disassembly shows, which is stable across runs while the mapped one is
   not. Stops the run rather than returning if it cannot resolve or execute:
   an override that silently skips the original body leaves the guest in a
   state nothing downstream can explain. */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep);

#endif /* GUEST_BODY_H */
