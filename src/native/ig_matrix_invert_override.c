/*
 * ig_matrix_invert_override.c -- libIGMath.dll igMatrix44f::invert
 * (0x1001b540) as a native override over ig_matrix_invert.c.
 *
 * `igResult igMatrix44f::invert(const igMatrix44f &m)`, thiscall with the
 * igResult returned through a hidden pointer: [ESP+4] is the result, [ESP+8]
 * is `m`, and RET 8 pops both. Its adjoint and determinant calls are its own
 * and are answered here too; they were ~3% of in-game samples together.
 *
 * THE CONTRACT is the sixteen floats at `this` (written only on success), the
 * igResult word, ESP, EAX, ECX and EDX, and the x87 stack and status word:
 * balanced, with C3/C2/C0 from the FCOMP of |(float)det| against FLT_MIN.
 * ESI and EDI are restored. Everything the body leaves on the stack is below
 * the returned ESP, and dead.
 *
 *   success: EAX = result, ECX = the success code, EDX = temp - this,
 *            where temp is the adjoint's stack temporary at entry ESP - 0x40.
 *   failure: EAX = result, ECX = the failure code's address, EDX = the code.
 *
 * The codes are libIGCore's, read through libIGMath's import slots.
 *
 * `math.invert_verify` re-runs the guest body after every native answer and
 * aborts on the first difference in that contract. `math.invert=0` turns the
 * override off.
 */
#include "ig_matrix_invert.h"
#include "x87_exact.h"

#include "guest_body.h"
#include "guest_memory.h"
#include "override_leaf.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LIBIGMATH "libIGMath.dll"
#define LIBIGMATH_PREFERRED_BASE 0x10000000u
#define INVERT_EP 0x1001b540u
/* The adjoint's eight-deep evaluation is the body's deepest point. */
#define INVERT_PUSHES 8u
#define MATRIX_BYTES 64u
#define SUCCESS_SLOT_LINKED 0x10029214u
#define FAILURE_SLOT_LINKED 0x10029218u
/* Where the adjoint's temporary sits, below the entry ESP. */
#define TEMP_BELOW_ENTRY 0x40u

static int s_enabled = -1;
static int s_verify = -1;
static uint32_t s_libigmath_base;
static uint64_t s_verified;

static int enabled(void) {
  if (__builtin_expect(s_enabled < 0, 0)) {
    s_enabled = lucent_cvar_flag("math.invert", 1) ? 1 : 0;
    s_verify = lucent_cvar_flag("math.invert_verify", 0) ? 1 : 0;
  }
  return s_enabled;
}

static uint32_t libigmath_mapped(uint32_t linked) {
  if (__builtin_expect(!s_libigmath_base, 0)) {
    s_libigmath_base = x86_module_base(LIBIGMATH);
    if (!s_libigmath_base) {
      x2_log_error("ig_matrix_invert: %s is not mapped, yet its invert "
                   "override ran; not continuing.\n",
                   LIBIGMATH);
      abort();
    }
  }
  return s_libigmath_base + (linked - LIBIGMATH_PREFERRED_BASE);
}

static void verify_or_abort(const CPU *C, const CPU *native,
                            const float out[16], uint32_t result_code) {
  CPU guest = *C;
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t result = RD32(C->reg[kX86pEsp] + 4u);
  x86_guest_body(&guest, LIBIGMATH, INVERT_EP);
  float guest_out[16];
  guest_memory_read(self, guest_out, MATRIX_BYTES);
  const int same =
      guest.reg[kX86pEsp] == native->reg[kX86pEsp] &&
      guest.reg[kX86pEax] == native->reg[kX86pEax] &&
      guest.reg[kX86pEcx] == native->reg[kX86pEcx] &&
      guest.reg[kX86pEdx] == native->reg[kX86pEdx] &&
      guest.x87.top == native->x87.top &&
      memcmp(guest.x87.tag, native->x87.tag, sizeof guest.x87.tag) == 0 &&
      guest.x87.control == native->x87.control &&
      guest.x87.status == native->x87.status && RD32(result) == result_code &&
      memcmp(guest_out, out, MATRIX_BYTES) == 0;
  if (!same) {
    x2_log_error(
        "math.invert_verify: the native %s!0x%08x disagrees with the guest "
        "body: eax %08x/%08x, ecx %08x/%08x, edx %08x/%08x, esp %08x/%08x, "
        "status %04x/%04x, result %08x/%08x, matrix %s. ig_matrix_invert.c "
        "is wrong; not continuing.\n",
        LIBIGMATH, INVERT_EP, native->reg[kX86pEax], guest.reg[kX86pEax],
        native->reg[kX86pEcx], guest.reg[kX86pEcx], native->reg[kX86pEdx],
        guest.reg[kX86pEdx], native->reg[kX86pEsp], guest.reg[kX86pEsp],
        native->x87.status, guest.x87.status, result_code, RD32(result),
        memcmp(guest_out, out, MATRIX_BYTES) ? "differs" : "same");
    abort();
  }
  s_verified++;
  if ((s_verified & (s_verified - 1u)) == 0u &&
      (s_verified & 0x5555555555555555ull)) {
    x2_log_info("math.invert_verify: %llu native answers match the guest "
                "body\n",
                (unsigned long long)s_verified);
  }
}

/* The whole call, RET included, where the native answer is exact; 0, having
   changed nothing, where the guest body must run. */
static int invert_native(CPU *C) {
  if (!enabled() || !x87_exact_for_guest(&C->x87, INVERT_PUSHES)) {
    return 0;
  }
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t result = RD32(esp + 4u);
  float m[16];
  float out[16];
  uint16_t codes = 0;
  guest_memory_read(RD32(esp + 8u), m, MATRIX_BYTES);
  const IgInvertVerdict verdict = ig_matrix44_invert(out, m, &codes);
  if (verdict == kIgInvertUndecided) {
    return 0;
  }
  CPU native = *C;
  native.reg[kX86pEsp] = esp + 12u;
  native.reg[kX86pEax] = result;
  native.x87.status =
      (uint16_t)((C->x87.status & ~kIgInvertCompareMask) | codes);
  uint32_t code;
  if (verdict == kIgInvertInverted) {
    code = RD32(RD32(libigmath_mapped(SUCCESS_SLOT_LINKED)));
    native.reg[kX86pEcx] = code;
    native.reg[kX86pEdx] = esp - TEMP_BELOW_ENTRY - self;
  } else {
    const uint32_t failure = RD32(libigmath_mapped(FAILURE_SLOT_LINKED));
    code = RD32(failure);
    native.reg[kX86pEcx] = failure;
    native.reg[kX86pEdx] = code;
    guest_memory_read(self, out, MATRIX_BYTES);
  }
  if (s_verify) {
    verify_or_abort(C, &native, out, code);
  }
  if (verdict == kIgInvertInverted) {
    guest_memory_write(self, out, MATRIX_BYTES);
  }
  WR32(result, code);
  C->reg[kX86pEax] = native.reg[kX86pEax];
  C->reg[kX86pEcx] = native.reg[kX86pEcx];
  C->reg[kX86pEdx] = native.reg[kX86pEdx];
  C->reg[kX86pEsp] = native.reg[kX86pEsp];
  C->x87.status = native.x87.status;
  return 1;
}

void x2_override_1001b540(CPU *C) {
  if (!invert_native(C)) {
    x86_guest_body(C, LIBIGMATH, INVERT_EP);
  }
}

/* Declined while math.invert_verify runs the guest body after each answer. */
static int invert_leaf(CPU *C) {
  return enabled() && !s_verify && invert_native(C);
}

__attribute__((constructor)) static void ig_matrix_invert_register(void) {
  x86_register_override(LIBIGMATH, INVERT_EP, x2_override_1001b540);
  x86_register_override_leaf(LIBIGMATH, INVERT_EP, invert_leaf);
}
