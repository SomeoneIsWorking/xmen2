/*
 * The host Direct3D 8 -- what the rest of x2native switches on and asks about.
 *
 * ---- why this exists, next to src/vulkan ----
 *
 * The renderer had been substituted one level higher: igVkVisualContext
 * registers with ARK as a subclass of igDx8VisualContext and replaces its
 * vtable (C115/C118). That got the engine to open a display and create a real
 * GPU device, and it measured what the remaining work is -- which is what
 * changed the plan. C127 found only 5 of the 98 device-touching slots can be
 * inherited; C128 found the other 78 funnel into 46 methods of ONE COM
 * interface; and running it showed the engine stopping in igDx8DecalExt, a
 * DIFFERENT ARK class that also reaches the device. There are ten such classes
 * (C113), and helpers below their vtables reach the device too.
 *
 * All of them reach it the same way, and C108 measured where: the entire
 * DirectX surface of the whole game is TWO imports, of which the renderer's is
 * d3d8.dll!Direct3DCreate8. So the narrowest cut that covers every one of the
 * ten classes at once is not above the engine's DirectX code -- it is BELOW
 * it, at the import the engine calls to get its Direct3D in the first place.
 *
 * What that buys, and it is the whole argument: the engine's own bodies run
 * UNMODIFIED. igDxVisualContext::setBlendingState still consults its override
 * global, still caches the value at this+0x2e4, and still calls
 * SetRenderState -- exactly as it does on Windows. Nothing here re-derives
 * engine behaviour, so nothing here can get it subtly wrong. The 73 vtable
 * offsets C108 measured across the whole COM family are the entire surface.
 *
 * ---- what would send us back the other way ----
 *
 * If the engine's Dx8 path turns out to need something a host device cannot
 * honestly answer -- a lock on a real D3D surface whose contents it then
 * reads back, say -- then that specific method is where the ARK substitution
 * earns its place again, for that class only. The two are not exclusive; the
 * device is simply the cheaper cut for the overwhelming majority.
 */
#ifndef D3D8_HOST_H
#define D3D8_HOST_H

#include <stdint.h>

/*
 * Take over d3d8.dll!Direct3DCreate8.
 *
 * Call before the guest runs. Until this is called the import behaves as it
 * did before -- it reports itself unimplemented -- so a build with the host
 * D3D8 linked but not enabled is indistinguishable from one without it, which
 * is what makes the two paths comparable in the same binary.
 */
void d3d8_host_enable(void);
int  d3d8_host_enabled(void);

/* Printed at shutdown: what was created, what was drawn, what was skipped. A
   run that presented no frames and a run whose renderer was never reached look
   identical on a black screen; this is what tells them apart. */
void d3d8_host_report(void);

/* The whole layer's self-test: ABI tables, vtable dispatch, the reporter, and
   the caps block. Returns the number of failures. */
int d3d8_host_selftest(void);

/* The IDirect3D8 singleton this host handed out: its guest address, its
   reference count, a way to create it without the import (for the self-test),
   and a counted reference for IDirect3DDevice8::GetDirect3D. */
uint32_t d3d8_the_direct3d8(void);
unsigned d3d8_the_direct3d8_refs(void);
void     d3d8_the_direct3d8_ensure(void);
int      d3d8_the_direct3d8_addref(void);

#endif /* D3D8_HOST_H */
