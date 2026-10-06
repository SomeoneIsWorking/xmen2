/*
 * d3d8_leaf_methods.h -- the COM methods a guest CALL completes in place.
 *
 * The engine reaches every D3D8 method through a vtable, so each call is a
 * CALL through memory. x86port gives such a CALL a leaf site (see
 * override_leaf.h), and a method named here is completed from inside the
 * translated block instead of ending it: the per-draw state setters and the
 * draws themselves are several million calls a minute.
 *
 * A method belongs here only when its implementation, and everything it
 * reaches, never runs guest code and never releases the guest lock. Present
 * and Reset do not qualify. The runtime guard in override_leaf.c aborts,
 * naming the method, if one named here ever does.
 */
#ifndef X2_D3D8_LEAF_METHODS_H
#define X2_D3D8_LEAF_METHODS_H

/* Register the named methods' thunks as leaves. Called once the interfaces'
   vtables exist. */
void d3d8_leaf_methods_install(void);

#endif
