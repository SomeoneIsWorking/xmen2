/*
 * skin_override.c -- libIGMath.dll 0x10022df0 and 0x10022e80 as native
 * overrides over skin.c.
 *
 * Both are cdecl, with eight stack arguments:
 *
 *   blend(positions, count, weights, indices, bones, matrices, out, stride)
 *   rigid(positions, count, unused, indices, index_stride, matrices, out,
 *         stride)
 *
 * THE CONTRACT is the output vertices, ESP, the volatile EAX, ECX and EDX the
 * bodies leave, and the two argument slots they write: count, decremented to
 * 0 by both, and blend's out pointer, advanced past the last vertex. The XMM
 * registers are volatile in this ABI and hold only the last vertex's
 * intermediates; they are not reproduced.
 *
 * Each override answers natively where the guest body would run to
 * completion: a count and bone count of at least one -- zero in either wraps
 * the guest's loop counter to 2^32 -- and positions and matrices 16-byte
 * aligned, which the body's MOVAPS loads fault without. Otherwise it runs the
 * guest body.
 *
 * `math.skin_verify` re-runs the guest body after every native answer and
 * aborts on the first difference in that contract. `math.skin=0` turns the
 * overrides off.
 */
#include "skin.h"

#include "guest_body.h"
#include "guest_memory.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LIBIGMATH "libIGMath.dll"
#define BLEND_EP 0x10022df0u
#define RIGID_EP 0x10022e80u
/* Stack argument offsets from ESP at entry. */
#define ARG_POSITIONS 4u
#define ARG_COUNT 8u
#define ARG_WEIGHTS 12u
#define ARG_INDICES 16u
#define ARG_BONES 20u /* rigid: the index stride */
#define ARG_MATRICES 24u
#define ARG_OUT 28u
#define ARG_STRIDE 32u
#define POSITION_BYTES 16u
#define MATRIX_BYTES 64u
#define OUT_BYTES 12u

typedef struct SkinCall {
  uint32_t positions;
  uint32_t count;
  uint32_t weights;
  uint32_t indices;
  uint32_t bones; /* rigid: the index stride */
  uint32_t matrices;
  uint32_t out;
  uint32_t stride;
} SkinCall;

/* The guest's end state for the contract's registers. */
typedef struct SkinResult {
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
} SkinResult;

/* -1 until first use, then the cvar's answer. */
static int s_enabled = -1;
static int s_verify = -1;
/* Verify mode's coverage: native answers checked, blend then rigid. */
static uint64_t s_verified[2];

static int enabled(void) {
  if (__builtin_expect(s_enabled < 0, 0)) {
    s_enabled = lucent_cvar_flag("math.skin", 1) ? 1 : 0;
    s_verify = lucent_cvar_flag("math.skin_verify", 0) ? 1 : 0;
  }
  return s_enabled;
}

static SkinCall read_call(uint32_t esp) {
  SkinCall call = {
      RD32(esp + ARG_POSITIONS), RD32(esp + ARG_COUNT),
      RD32(esp + ARG_WEIGHTS),   RD32(esp + ARG_INDICES),
      RD32(esp + ARG_BONES),     RD32(esp + ARG_MATRICES),
      RD32(esp + ARG_OUT),       RD32(esp + ARG_STRIDE),
  };
  return call;
}

static int native_runs(const SkinCall *call, int blend) {
  return enabled() && call->count != 0u && (!blend || call->bones != 0u) &&
         (call->positions & 15u) == 0u && (call->matrices & 15u) == 0u;
}

static const float *position_at(const SkinCall *call, uint32_t vertex) {
  return guest_memory_const_pointer(call->positions + vertex * POSITION_BYTES);
}

static SkinResult run_blend(const SkinCall *call) {
  const float *matrices = guest_memory_const_pointer(call->matrices);
  for (uint32_t v = 0; v < call->count; v++) {
    const uint32_t first = v * call->bones;
    float out[3];
    skin_blend_vertex(out, position_at(call, v),
                      guest_memory_const_pointer(call->indices + first),
                      guest_memory_const_pointer(call->weights + first * 4u),
                      call->bones, matrices);
    guest_memory_write(call->out + v * call->stride, out, OUT_BYTES);
  }
  const uint32_t end = call->out + call->count * call->stride;
  const SkinResult result = {call->indices + call->count * call->bones, 0u,
                             end};
  return result;
}

static SkinResult run_rigid(const SkinCall *call) {
  const float *matrices = guest_memory_const_pointer(call->matrices);
  uint8_t index = 0;
  for (uint32_t v = 0; v < call->count; v++) {
    float out[3];
    index = *(const uint8_t *)guest_memory_const_pointer(call->indices +
                                                         v * call->bones);
    skin_rigid_vertex(out, position_at(call, v), index, matrices);
    guest_memory_write(call->out + v * call->stride, out, OUT_BYTES);
  }
  const SkinResult result = {call->indices + call->count * call->bones,
                             call->bones, (uint32_t)index * MATRIX_BYTES};
  return result;
}

/* The guest body from the state the override started in, which must end
   where the native answer did. The argument slots the native answer wrote
   are put back first: the body reads them. */
static void verify_or_abort(const CPU *C, uint32_t ep, const SkinCall *call,
                            SkinResult native) {
  const size_t bytes = (size_t)call->count * OUT_BYTES;
  float *native_out = malloc(bytes);
  if (!native_out) {
    x2_log_error("math.skin_verify: no memory for %u vertices; not "
                 "continuing.\n",
                 call->count);
    abort();
  }
  for (uint32_t v = 0; v < call->count; v++) {
    guest_memory_read(call->out + v * call->stride, &native_out[v * 3u],
                      OUT_BYTES);
  }
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t native_count = RD32(esp + ARG_COUNT);
  const uint32_t native_out_slot = RD32(esp + ARG_OUT);
  WR32(esp + ARG_COUNT, call->count);
  WR32(esp + ARG_OUT, call->out);
  CPU guest = *C;
  x86_guest_body(&guest, LIBIGMATH, ep);
  int same =
      guest.reg[kX86pEsp] == esp + 4u && guest.reg[kX86pEax] == native.eax &&
      guest.reg[kX86pEcx] == native.ecx && guest.reg[kX86pEdx] == native.edx &&
      RD32(esp + ARG_COUNT) == native_count &&
      RD32(esp + ARG_OUT) == native_out_slot;
  uint32_t v = 0;
  for (; same && v < call->count; v++) {
    float guest_out[3];
    guest_memory_read(call->out + v * call->stride, guest_out, OUT_BYTES);
    same = memcmp(guest_out, &native_out[v * 3u], OUT_BYTES) == 0;
  }
  free(native_out);
  if (!same) {
    x2_log_error("math.skin_verify: the native %s!0x%08x disagrees with the "
                 "guest body: eax %08x/%08x, ecx %08x/%08x, edx %08x/%08x, "
                 "count slot %u/%u, vertex %u of %u. skin.c is wrong; not "
                 "continuing.\n",
                 LIBIGMATH, ep, native.eax, guest.reg[kX86pEax], native.ecx,
                 guest.reg[kX86pEcx], native.edx, guest.reg[kX86pEdx],
                 native_count, RD32(esp + ARG_COUNT), v ? v - 1u : 0u,
                 call->count);
    abort();
  }
  const uint64_t total =
      ++s_verified[ep == RIGID_EP] + s_verified[ep != RIGID_EP];
  if ((total & (total - 1u)) == 0u && (total & 0x5555555555555555ull)) {
    x2_log_info("math.skin_verify: %llu native answers match the guest body "
                "(blend %llu, rigid %llu)\n",
                (unsigned long long)total, (unsigned long long)s_verified[0],
                (unsigned long long)s_verified[1]);
  }
}

static void finish(CPU *C, uint32_t ep, const SkinCall *call, SkinResult result,
                   int blend) {
  const uint32_t esp = C->reg[kX86pEsp];
  WR32(esp + ARG_COUNT, 0u);
  if (blend) {
    WR32(esp + ARG_OUT, result.edx);
  }
  if (s_verify) {
    verify_or_abort(C, ep, call, result);
  }
  C->reg[kX86pEax] = result.eax;
  C->reg[kX86pEcx] = result.ecx;
  C->reg[kX86pEdx] = result.edx;
  C->reg[kX86pEsp] = esp + 4u;
}

void x2_override_10022df0(CPU *C) {
  const SkinCall call = read_call(C->reg[kX86pEsp]);
  if (!native_runs(&call, 1)) {
    x86_guest_body(C, LIBIGMATH, BLEND_EP);
    return;
  }
  finish(C, BLEND_EP, &call, run_blend(&call), 1);
}

void x2_override_10022e80(CPU *C) {
  const SkinCall call = read_call(C->reg[kX86pEsp]);
  if (!native_runs(&call, 0)) {
    x86_guest_body(C, LIBIGMATH, RIGID_EP);
    return;
  }
  finish(C, RIGID_EP, &call, run_rigid(&call), 0);
}

__attribute__((constructor)) static void skin_register_overrides(void) {
  x86_register_override("libIGMath.dll", BLEND_EP, x2_override_10022df0);
  x86_register_override("libIGMath.dll", RIGID_EP, x2_override_10022e80);
}
