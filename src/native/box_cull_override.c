/*
 * box_cull_override.c -- libIGSg.dll 0x10047570 and 0x100478e0 as native
 * overrides over box_cull.c.
 *
 * Each override answers natively only where that is exact: box_cull's host
 * test, the guest's own control word X86P_X87_CW_INIT, and enough empty x87
 * registers below TOP for every push the guest body would make, so that the
 * body could not have taken a stack fault. Otherwise, and for classify's
 * guard-band case, it runs the guest body.
 *
 * THE CONTRACT is the functions' calling convention: the output buffer, EAX,
 * ESP, and the x87 TOP, tags, control and status words. Neither body leaves
 * anything else a caller can depend on -- the registers it pops keep values
 * nothing reads before the next push, and ECX/EDX are caller-saved scratch.
 *
 * `sg.box_cull_verify` re-runs the guest body after every native answer and
 * aborts on the first difference in that contract. `sg.box_cull=0` turns the
 * overrides off.
 */
#include "box_cull.h"

#include "guest_body.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LIBIGSG "libIGSg.dll"
#define LIBIGSG_PREFERRED_BASE 0x10000000u
#define CORNERS_EP 0x10047570u
#define CLASSIFY_EP 0x100478e0u
/* The .rdata 0.0f corners multiplies the extents it leaves out by. */
#define CORNERS_ZERO_LINKED 0x10077ba8u
/* The deepest each body pushes the x87 stack before its first pop back. */
#define CORNERS_PUSHES 6u
#define CLASSIFY_PUSHES 3u

/* -1 until first use, then the cvar's answer. */
static int s_enabled = -1;
static int s_verify = -1;
static uint32_t s_corners_zero;
/* Verify mode's coverage: native answers checked, per function, and classify
   calls the guard band sent to the guest body. */
static uint64_t s_verified[2];
static uint64_t s_undecided;

static int enabled(void) {
  if (__builtin_expect(s_enabled < 0, 0)) {
    s_enabled = lucent_cvar_flag("sg.box_cull", 1) ? 1 : 0;
    s_verify = lucent_cvar_flag("sg.box_cull_verify", 0) ? 1 : 0;
  }
  return s_enabled;
}

static int x87_pushes_fit(const X86pX87 *f, unsigned pushes) {
  for (unsigned i = 1; i <= pushes; i++) {
    if (f->tag[(f->top - i) & 7u] != (uint8_t)kX86pX87TagEmpty) {
      return 0;
    }
  }
  return 1;
}

static int native_exact(const CPU *C, unsigned pushes) {
  return enabled() && C->x87.control == X86P_X87_CW_INIT &&
         x87_pushes_fit(&C->x87, pushes) && box_cull_host_exact();
}

static void read_floats(uint32_t address, float *out, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    out[i] = x86_loadf32(address + i * 4u);
  }
}

static uint32_t corners_zero_address(void) {
  if (__builtin_expect(!s_corners_zero, 0)) {
    const uint32_t base = x86_module_base(LIBIGSG);
    if (!base) {
      x2_log_error("box_cull: %s is not mapped, yet its override at 0x%08x "
                   "ran; not continuing.\n",
                   LIBIGSG, CORNERS_EP);
      abort();
    }
    s_corners_zero = base + (CORNERS_ZERO_LINKED - LIBIGSG_PREFERRED_BASE);
  }
  return s_corners_zero;
}

/* The guest body on a copy of the state the override started from, which
   must end where the native answer did: the same EAX (classify only), ESP,
   x87 control state, and `out_floats` output words. */
static void verify_or_abort(const CPU *C, uint32_t ep, uint32_t native_eax,
                            uint32_t out, const float *native_out,
                            unsigned out_floats) {
  CPU guest = *C;
  x86_guest_body(&guest, LIBIGSG, ep);
  int same = guest.reg[kX86pEsp] == C->reg[kX86pEsp] + 4u &&
             guest.x87.top == C->x87.top &&
             memcmp(guest.x87.tag, C->x87.tag, sizeof guest.x87.tag) == 0 &&
             guest.x87.control == C->x87.control &&
             guest.x87.status == C->x87.status &&
             (ep != CLASSIFY_EP || guest.reg[kX86pEax] == native_eax);
  unsigned word = 0;
  for (; same && word < out_floats; word++) {
    uint32_t native_word;
    memcpy(&native_word, &native_out[word], sizeof native_word);
    same = RD32(out + word * 4u) == native_word;
  }
  if (!same) {
    x2_log_error("sg.box_cull_verify: the native %s!0x%08x disagrees with "
                 "the guest body: eax %08x/%08x, esp %08x/%08x, x87 status "
                 "%04x/%04x, top %u/%u, output word %u of %u. box_cull.c is "
                 "wrong; not continuing.\n",
                 LIBIGSG, ep, native_eax, guest.reg[kX86pEax],
                 C->reg[kX86pEsp] + 4u, guest.reg[kX86pEsp], C->x87.status,
                 guest.x87.status, C->x87.top, guest.x87.top,
                 word ? word - 1u : 0u, out_floats);
    abort();
  }
  const uint64_t total =
      ++s_verified[ep == CLASSIFY_EP] + s_verified[ep != CLASSIFY_EP];
  if ((total & (total - 1u)) == 0u && (total & 0x5555555555555555ull)) {
    x2_log_info("sg.box_cull_verify: %llu native answers match the guest "
                "body (corners %llu, classify %llu; %llu classify calls "
                "undecided, run as guest body)\n",
                (unsigned long long)total, (unsigned long long)s_verified[0],
                (unsigned long long)s_verified[1],
                (unsigned long long)s_undecided);
  }
}

void x2_override_10047570(CPU *C) {
  if (!native_exact(C, CORNERS_PUSHES)) {
    x86_guest_body(C, LIBIGSG, CORNERS_EP);
    return;
  }
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t out = RD32(esp + 4u);
  float min[3];
  float extent[3];
  float matrix[16];
  float corners[BOX_CULL_CORNER_FLOATS];
  read_floats(RD32(esp + 8u), min, 3u);
  read_floats(RD32(esp + 12u), extent, 3u);
  read_floats(RD32(esp + 16u), matrix, 16u);
  box_cull_corners(corners, min, extent, matrix,
                   x86_loadf32(corners_zero_address()));
  for (unsigned i = 0; i < BOX_CULL_CORNER_FLOATS; i++) {
    uint32_t word;
    memcpy(&word, &corners[i], sizeof word);
    WR32(out + i * 4u, word);
  }
  if (s_verify) {
    verify_or_abort(C, CORNERS_EP, C->reg[kX86pEax], out, corners,
                    BOX_CULL_CORNER_FLOATS);
  }
  C->reg[kX86pEsp] = esp + 4u;
}

void x2_override_100478e0(CPU *C) {
  if (!native_exact(C, CLASSIFY_PUSHES)) {
    x86_guest_body(C, LIBIGSG, CLASSIFY_EP);
    return;
  }
  const uint32_t esp = C->reg[kX86pEsp];
  float corners[BOX_CULL_CORNER_FLOATS];
  read_floats(RD32(esp + 4u), corners, BOX_CULL_CORNER_FLOATS);
  const BoxCullVerdict verdict = box_cull_classify(corners);
  if (verdict == kBoxCullUndecided) {
    s_undecided += (uint64_t)s_verify;
    x86_guest_body(C, LIBIGSG, CLASSIFY_EP);
    return;
  }
  if (s_verify) {
    verify_or_abort(C, CLASSIFY_EP, (uint32_t)verdict, 0u, NULL, 0u);
  }
  C->reg[kX86pEax] = (uint32_t)verdict;
  C->reg[kX86pEsp] = esp + 4u;
}

__attribute__((constructor)) static void box_cull_register_overrides(void) {
  x86_register_override("libIGSg.dll", CORNERS_EP, x2_override_10047570);
  x86_register_override("libIGSg.dll", CLASSIFY_EP, x2_override_100478e0);
}
