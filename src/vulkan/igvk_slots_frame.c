/*
 * igVkVisualContext -- the frame: the scene boundary, the clear, the viewport.
 *
 * igDxVisualContext's frame is D3D8-shaped -- BeginScene, Clear, SetViewport,
 * draws, EndScene, Present -- and these four slots are where it touches the
 * device. gpu_device.c maps that shape onto SDL_GPU's; see its file comment
 * for why the clear has to be deferred to the render pass.
 *
 * Slot numbers and `RET N` from tools/device_slots.py --list.
 */
#include "gpu_device.h"
#include "igvk_context.h"

#include <stdio.h>

#define DX_SET_VIEWPORT 0x1002ec70u /* slot 186, ret 0x18 -> 6 args */
#define DX_RD_IS_USABLE                                                        \
  0x1002b5b0u /* the pool-entry check setRenderDestination makes */

/* The render-destination pool: this+0x178, entries of 0x38 bytes at
 * [pool+0x10]. */
#define F_RD_POOL 0x178u
#define F_RD_CURRENT 0x17cu
#define RD_STRIDE 0x38u
#define RD_STATE 0x1cu /* == 2 means "not usable" */

/*
 * The fields igDxVisualContext keeps the frame state in, read out of its own
 * clearRenderDestination and setViewport bodies.
 */
#define F_CLEAR_R 0x190u /* float */
#define F_CLEAR_G 0x194u
#define F_CLEAR_B 0x198u
#define F_CLEAR_A 0x19cu
#define F_CLEAR_DEPTH 0x1a0u /* float */
#define F_CLEAR_STENC 0x1a4u /* uint32 */
#define F_VIEW_X 0x1a8u      /* int, as REQUESTED (pre-clamp) */
#define F_VIEW_Y 0x1acu
#define F_VIEW_W 0x1b0u
#define F_VIEW_H 0x1b4u
#define F_VIEW_MINZ 0x1b8u /* float */
#define F_VIEW_MAXZ 0x1bcu /* float */

/*
 * Program the GPU viewport from the fields the engine's setViewport stored.
 *
 * Shared by slot 186 and by slot 47's trailing call, because both have to do
 * the same thing after the engine's body has run: the fields hold the
 * REQUEST, the clamped rectangle only ever existed in the engine's stack
 * frame, and the depth range is clamped to [0,1] exactly as the body does.
 */
static void apply_viewport_from_fields(uint32_t self) {
  float minz = igvk_fieldf(self, F_VIEW_MINZ);
  float maxz = igvk_fieldf(self, F_VIEW_MAXZ);

  if (minz < 0.0f)
    minz = 0.0f;
  else if (minz > 1.0f)
    minz = 1.0f;
  if (maxz < 0.0f)
    maxz = 0.0f;
  else if (maxz > 1.0f)
    maxz = 1.0f;

  gpu_frame_viewport((int32_t)RD32(self + F_VIEW_X),
                     (int32_t)RD32(self + F_VIEW_Y),
                     (int32_t)RD32(self + F_VIEW_W),
                     (int32_t)RD32(self + F_VIEW_H), minz, maxz);
}

/* ---- slot 174: beginDraw ---------------------------------------------- */

/*
 * The engine's body is:
 *
 *     if (this->getLastError()) return false;     // own vtable slot 34
 *     device->BeginScene();
 *     this->f44 = <timer>;
 *     return true;
 *
 * Implemented directly, not super-called: the BeginScene is unguarded, and
 * everything else here is three lines. The timer store at this+0x44 pairs
 * with endDraw's frame-time bookkeeping -- see there for why that pair is not
 * reproduced and how you would know if it started to matter.
 *
 * The bool result matters: the engine skips the whole frame when it is false,
 * which is exactly what should happen when there is no swapchain image.
 */
static void vk_begin_draw(CPU *C) {
  if (!gpu_device_ready()) {
    ark_ret(C, 0, 0);
    return;
  }
  ark_ret(C, gpu_frame_begin() ? 1u : 0u, 0);
}

/* ---- slot 175: endDraw ------------------------------------------------- */

/*
 * The engine's body, read out at libIGGfx 0x1002eb70:
 *
 *     if (this->deviceLost) return;               // this+0x15c
 *     if (this->profiling)  <read the timer>;     // this+0x3c
 *     ++g_frameCount;                             // 64-bit, 0x101895b0
 *     device->EndScene();
 *     if (device->Present(0,0,0,0) == D3DERR_DEVICELOST) this->deviceLost = 1;
 *     if (this->profiling)  <accumulate frame time>;
 *
 * The counter is kept because it is engine state other code may read, and
 * incrementing it is two lines. The PROFILING branch is not reproduced: it
 * calls back through the timer object at this+0x38 for a float result, and
 * this host's guest-call helper returns EAX, not ST0, so making that call
 * would need FPU-result plumbing that does not exist yet.
 *
 * That is a real gap, so it is not left silent -- this+0x3c is checked, and
 * if the engine ever turns profiling on, it says so by name instead of the
 * frame timings quietly reading zero.
 */
#define G_FRAME_COUNT_LO 0x101895b0u
#define F_DEVICE_LOST 0x15cu
#define F_PROFILING 0x3cu

static void vk_end_draw(CPU *C) {
  uint32_t self = IGVK_SELF(C);
  uint32_t lo_va = ark_lifted(IGVK_GFX, G_FRAME_COUNT_LO);

  if (RD8(self + F_DEVICE_LOST)) {
    ark_ret(C, 0, 0);
    return;
  }

  if (RD8(self + F_PROFILING)) {
    static int told;
    if (!told++)
      fprintf(stderr,
              "igVk: the engine has frame profiling ON (this+0x3c), and "
              "this backend does NOT reproduce endDraw's timing branch "
              "-- calling the timer at this+0x38 needs an FPU return "
              "path the guest-call helper does not have. Frame times "
              "the engine reports will be wrong, not merely absent.\n");
  }
  if (lo_va) {
    /* 64-bit counter, little-endian pair, exactly as ADD/ADC does it. */
    uint32_t lo = RD32(lo_va) + 1u;
    WR32(lo_va, lo);
    if (lo == 0u)
      WR32(lo_va + 4u, RD32(lo_va + 4u) + 1u);
  }
  gpu_frame_end();
  ark_ret(C, 0, 0);
}

/* ---- slot 177: clearRenderDestination ---------------------------------- */

/*
 * clearRenderDestination(flags), RET 4. `flags` is a byte mask: bit 0 colour,
 * bit 1 depth, bit 2 stencil -- read straight off the body's TEST AL,1 / 2 / 4.
 *
 * The engine reads the clear colour from its own fields as four floats,
 * multiplies each by 255 and packs ARGB for D3D. Here they are used as floats
 * directly, so no packing happens and no rounding is introduced.
 *
 * Not super-called: the body's Clear is unguarded, and there is nothing to
 * inherit -- every value it uses is already in the object's fields.
 *
 * ONE thing the engine's body does that this does not: when a stencil clear
 * is asked for, it clamps this+0x1a4 down to the stencil bits the render
 * destination actually has. Reproducing that means indexing the render
 * destination pool, and no stencil buffer exists in this backend yet, so the
 * value is passed through unclamped. It will need doing when depth/stencil
 * targets land.
 */
static void vk_clear_render_destination(CPU *C) {
  uint32_t self = IGVK_SELF(C);
  unsigned mask = IGVK_ARG(C, 0) & 0x7u;

  gpu_frame_clear(mask, igvk_fieldf(self, F_CLEAR_R),
                  igvk_fieldf(self, F_CLEAR_G), igvk_fieldf(self, F_CLEAR_B),
                  igvk_fieldf(self, F_CLEAR_A),
                  igvk_fieldf(self, F_CLEAR_DEPTH), RD32(self + F_CLEAR_STENC));
  ark_ret(C, 0, 1);
}

/* ---- slot 186: setViewport --------------------------------------------- */

/*
 * setViewport(x, y, w, h, minz, maxz), RET 0x18.
 *
 * 395 instructions, of which one is the device call: the rest clamps the
 * requested rectangle against the current render destination's size and the
 * depth range into [0,1], and stores the request at this+0x1a8..0x1bc. The
 * body guards its device call (`MOV ECX,[ECX+0x144]; TEST ECX,ECX; JZ`), so
 * it is super-called and does all of that itself. Rewriting that clamp by
 * hand is precisely the re-derivation this design exists to avoid.
 *
 * What the fields then hold is the REQUEST, not the clamped result -- the
 * clamped rectangle only ever lived in the engine's stack frame. So the
 * clamp is applied again here, against the swapchain. The two agree whenever
 * the render destination IS the back buffer, which is the only destination
 * this backend has; when off-screen targets land, this must clamp against the
 * bound target instead and the difference will matter.
 */
static void vk_set_viewport(CPU *C) {
  uint32_t self = IGVK_SELF(C);
  uint32_t r = igvk_super(C, DX_SET_VIEWPORT, 6);
  apply_viewport_from_fields(self);
  ark_ret(C, r, 6);
}

/* ---- slot 47: setRenderDestination ------------------------------------- */

/*
 * setRenderDestination(index, arg1), RET 8.
 *
 * Transcribed from libIGGfx 0x1002aaf0 rather than super-called, because its
 * device call is UNGUARDED -- `MOV EAX,[ESI+0x144]; MOV ECX,[EAX]` on a NULL
 * device. Measured, not assumed; slot 50 next door IS guarded and is
 * super-called for exactly that reason.
 *
 * The engine's body, all 47 instructions of it:
 *
 *     idx   = arg0 & 0xffff
 *     entry = [[this+0x178] + 0x10] + idx*0x38
 *     if (!entry || entry->state == 2)            return;
 *     if (!this->isUsable(entry, 0))              return;    // 0x1002b5b0
 *     device->SetRenderTarget(entry->f24, entry->f28);       // vtable +0x7c
 *     this->current = idx;
 *     this->vtbl[186](f1a8, f1ac, f1b0, f1b4, 0, 1.0f);      // setViewport
 *
 * Only the SetRenderTarget is replaced -- everything else is the engine's own,
 * including the final setViewport, which goes through the object's own vtable
 * so it lands on our slot 186 and programs the GPU.
 *
 * The two surfaces the engine would have passed (entry+0x24 colour, entry+0x28
 * depth/stencil) are D3D surface pointers this host never created. What
 * matters is WHICH destination was selected, so the index is what is handed to
 * the device layer.
 */
static void vk_set_render_destination(CPU *C) {
  uint32_t self = IGVK_SELF(C);
  uint32_t idx = IGVK_ARG(C, 0) & 0xffffu;
  uint32_t pool = RD32(self + F_RD_POOL);
  uint32_t entry = pool ? RD32(pool + 0x10u) + idx * RD_STRIDE : 0;
  uint32_t chk, args[2], vt;

  if (!entry || RD32(entry + RD_STATE) == 2u) {
    ark_ret(C, 0, 2);
    return;
  }

  /*
   * The engine calls igDxVisualContext::checkAndCreateSurfaces here
   * (0x1002b5b0) and bails if it returns false. It is NOT called.
   *
   * That function's job is to CREATE the Direct3D colour and depth surfaces
   * for this destination, and it does so through the device unguarded -- it
   * took SIGSEGV at NULL the first time this slot called it. It is not a
   * predicate that can be answered without a device; it is device work.
   *
   * For this backend the answer is that the surfaces already exist and are
   * not D3D's: the swapchain image SDL hands out each frame is the colour
   * target. So "usable" is true for the back buffer, and the entry's surface
   * fields at +0x24/+0x28 stay NULL -- which is safe precisely because the
   * SetRenderTarget that would have consumed them is the call this slot
   * replaces.
   *
   * OFF-SCREEN destinations are the case this does not cover, and
   * gpu_frame_bind_target says so by index rather than letting them draw
   * silently to the wrong place.
   */
  (void)chk;
  (void)args;

  gpu_frame_bind_target(idx);
  WR32(self + F_RD_CURRENT, idx);

  /*
   * The engine's own trailing setViewport.
   *
   * This used to re-enter the object's OWN vtable -- ark_call_this on
   * RD32(vt + 186*4) -- so that an override of slot 186 would be honoured.
   * That unusual path was the first suspect when
   * igDxVisualContext::setupDrawing (0x1002ead0) RETed four words out, but
   * replacing it with the direct engine-body call changed nothing. The
   * controlled negative exposed the real cause: every nested host-to-guest
   * call reused the same scratch frame. igvk_ark.c now reserves and restores
   * a separate window per call (issue #23, C126).
   *
   * The direct engine-body call was retained because it is the simpler of
   * two equivalent paths, and applies the viewport here exactly as slot 186
   * does. The cost is that a future override of slot 186 is bypassed from
   * here; that is a real limitation and it is written down rather than left
   * to be discovered.
   */
  {
    uint32_t vp[6], fn = ark_lifted(IGVK_GFX, DX_SET_VIEWPORT);
    vp[0] = RD32(self + F_VIEW_X);
    vp[1] = RD32(self + F_VIEW_Y);
    vp[2] = RD32(self + F_VIEW_W);
    vp[3] = RD32(self + F_VIEW_H);
    vp[4] = 0u;
    vp[5] = 0x3f800000u; /* 1.0f */
    if (fn)
      ark_call_this(fn, self, vp, 6);
    apply_viewport_from_fields(self);
  }
  (void)vt;
  ark_ret(C, 0, 2);
}

/* ---- slot 50: setRenderDestinationSize --------------------------------- */

/*
 * RET 12. Its device call IS guarded (`MOV EAX,[ESI+0x144]; TEST EAX,EAX;
 * JZ`), so the engine's own body runs and does all the bookkeeping; the
 * viewport it recomputes reaches the GPU through slot 186.
 */
#define DX_SET_RD_SIZE 0x1002ad40u

static void vk_set_render_destination_size(CPU *C) {
  ark_ret(C, igvk_super(C, DX_SET_RD_SIZE, 3), 3);
}

/* ---- installation ------------------------------------------------------ */

void igvk_install_frame(void) {
  igvk_slot(47, vk_set_render_destination, "setRenderDestination");
  igvk_slot(50, vk_set_render_destination_size, "setRenderDestinationSize");
  igvk_slot(174, vk_begin_draw, "beginDraw");
  igvk_slot(175, vk_end_draw, "endDraw");
  igvk_slot(177, vk_clear_render_destination, "clearRenderDestination");
  igvk_slot(186, vk_set_viewport, "setViewport");
}
