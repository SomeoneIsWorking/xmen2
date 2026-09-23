/*
 * ig_matrix_override.c -- libIGMath.dll igMatrix44f::multiply (0x10019520)
 * as a native override over ig_matrix.c.
 *
 * `void igMatrix44f::multiply(const igMatrix44f &a, const igMatrix44f &b)`,
 * thiscall: this = a * b. When `this` is `a` or `b` the guest computes into a
 * stack temporary and copies it over `this` with an integer copy at
 * 0x10019b90; otherwise it stores each element as it is computed.
 *
 * THE CONTRACT is the sixteen floats at `this`, ESP, the x87 TOP, tags,
 * control and status words, and the volatile EAX, ECX and EDX the body
 * leaves, which differ between the two paths. The stack temporary is below
 * ESP on return and dead.
 *
 * It answers natively where that is exact: x87_exact_for_guest() with the
 * body's two pushes, and `this` either equal to an operand or clear of both.
 * A `this` that partly overlaps an operand reads its own stores in the guest;
 * that runs the guest body.
 *
 * `math.matrix_verify` re-runs the guest body after every native answer and
 * aborts on the first difference in that contract. `math.matrix=0` turns the
 * override off.
 */
#include "ig_matrix.h"
#include "x87_exact.h"

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
#define MULTIPLY_EP 0x10019520u
#define MULTIPLY_PUSHES 2u
#define MATRIX_BYTES 64u
/* Where the non-aliased path leaves EDX: b's third row, the last column's
   pointer after four steps. */
#define B_POINTER_END 0x30u

/* -1 until first use, then the cvar's answer. */
static int s_enabled = -1;
static int s_verify = -1;
static uint64_t s_verified;

static int enabled(void) {
  if (__builtin_expect(s_enabled < 0, 0)) {
    s_enabled = lucent_cvar_flag("math.matrix", 1) ? 1 : 0;
    s_verify = lucent_cvar_flag("math.matrix_verify", 0) ? 1 : 0;
  }
  return s_enabled;
}

static int overlaps(uint32_t x, uint32_t y) {
  return x - y < MATRIX_BYTES || y - x < MATRIX_BYTES;
}

static void verify_or_abort(const CPU *C, const CPU *native,
                            const float out[16]) {
  CPU guest = *C;
  x86_guest_body(&guest, LIBIGMATH, MULTIPLY_EP);
  const uint32_t self = C->reg[kX86pEcx];
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
      guest.x87.status == native->x87.status &&
      memcmp(guest_out, out, MATRIX_BYTES) == 0;
  if (!same) {
    x2_log_error(
        "math.matrix_verify: the native %s!0x%08x disagrees with the "
        "guest body: eax %08x/%08x, ecx %08x/%08x, edx %08x/%08x, "
        "esp %08x/%08x, output %s. ig_matrix.c is wrong; not "
        "continuing.\n",
        LIBIGMATH, MULTIPLY_EP, native->reg[kX86pEax], guest.reg[kX86pEax],
        native->reg[kX86pEcx], guest.reg[kX86pEcx], native->reg[kX86pEdx],
        guest.reg[kX86pEdx], native->reg[kX86pEsp], guest.reg[kX86pEsp],
        memcmp(guest_out, out, MATRIX_BYTES) ? "differs" : "same");
    abort();
  }
  s_verified++;
  if ((s_verified & (s_verified - 1u)) == 0u &&
      (s_verified & 0x5555555555555555ull)) {
    x2_log_info("math.matrix_verify: %llu native answers match the guest "
                "body\n",
                (unsigned long long)s_verified);
  }
}

void x2_override_10019520(CPU *C) {
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t self = C->reg[kX86pEcx];
  const uint32_t a = RD32(esp + 4u);
  const uint32_t b = RD32(esp + 8u);
  const int aliased = self == a || self == b;
  if (!enabled() || !x87_exact_for_guest(&C->x87, MULTIPLY_PUSHES) ||
      (!aliased && (overlaps(self, a) || overlaps(self, b)))) {
    x86_guest_body(C, LIBIGMATH, MULTIPLY_EP);
    return;
  }
  float ma[16];
  float mb[16];
  float out[16];
  guest_memory_read(a, ma, MATRIX_BYTES);
  guest_memory_read(b, mb, MATRIX_BYTES);
  ig_matrix44_multiply(out, ma, mb);
  CPU native = *C;
  native.reg[kX86pEsp] = esp + 12u;
  if (aliased) {
    /* The copy's last two moves: word 14 through EDX, word 15 through
       EAX. */
    uint32_t words[16];
    memcpy(words, out, sizeof words);
    native.reg[kX86pEax] = words[15];
    native.reg[kX86pEdx] = words[14];
  } else {
    native.reg[kX86pEax] = a;
    native.reg[kX86pEdx] = b + B_POINTER_END;
  }
  if (s_verify) {
    verify_or_abort(C, &native, out);
  }
  guest_memory_write(self, out, MATRIX_BYTES);
  C->reg[kX86pEax] = native.reg[kX86pEax];
  C->reg[kX86pEdx] = native.reg[kX86pEdx];
  C->reg[kX86pEsp] = native.reg[kX86pEsp];
}

__attribute__((constructor)) static void ig_matrix_register_overrides(void) {
  x86_register_override("libIGMath.dll", MULTIPLY_EP, x2_override_10019520);
}
