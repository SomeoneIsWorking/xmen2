/*
 * box_cull_override.c -- libIGSg.dll's box test as native overrides over
 * box_cull.c: the leaves 0x10047570 and 0x100478e0, and the driver at
 * 0x10047470 that calls them.
 *
 * THE DRIVER, `int box_test(traversal, box)`, cdecl, finds the current view
 * and projection matrices through two attribute stacks, has the traversal
 * recompute its composite matrix only when either changed, and then runs the
 * two leaves on the box. Natively it is one crossing where the guest body was
 * three. A changed matrix pair, and classify's guard-band case, run the guest
 * body: neither writes anything the body would not write again.
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
 *
 * A direct CALL to any of the three takes its native answer in place
 * (override_leaf.h); the cases that need the guest body, and every call while
 * verifying, decline and take the ordinary path.
 */
#include "box_cull.h"
#include "x87_exact.h"

#include "guest_body.h"
#include "override_leaf.h"
#include "x2_log.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <lucent/cvar_c.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LIBIGSG "libIGSg.dll"
#define LIBIGSG_PREFERRED_BASE 0x10000000u
#define DRIVER_EP 0x10047470u
#define CORNERS_EP 0x10047570u
#define CLASSIFY_EP 0x100478e0u
/* The .rdata 0.0f corners multiplies the extents it leaves out by. */
#define CORNERS_ZERO_LINKED 0x10077ba8u
/* The import slots of the two attribute-stack indices the driver reads. */
#define DRIVER_VIEW_INDEX_SLOT_LINKED 0x10070314u
#define DRIVER_PROJECTION_INDEX_SLOT_LINKED 0x10070310u
/* igFrustCullTraversal: its composite matrix, and the two matrices it was
   composed from. */
#define TRAVERSAL_ATTRIBUTES 0x34u
#define TRAVERSAL_COMPOSITE 0x1dcu
#define TRAVERSAL_COMPOSED_VIEW 0x21cu
#define TRAVERSAL_COMPOSED_PROJECTION 0x220u
/* The box's min and max, six floats. */
#define BOX_BOUNDS 0x8u
/* The deepest each body pushes the x87 stack before its first pop back; the
   driver's own pushes are one deep, under its callees'. */
#define CORNERS_PUSHES 6u
#define CLASSIFY_PUSHES 3u

/* -1 until first use, then the cvar's answer. */
static int s_enabled = -1;
static int s_verify = -1;
static uint32_t s_libigsg_base;
/* Verify mode's coverage: native answers checked, per function, and classify
   calls the guard band sent to the guest body. */
static uint64_t s_verified[3];
static uint64_t s_undecided;

static int enabled(void) {
  if (__builtin_expect(s_enabled < 0, 0)) {
    s_enabled = lucent_cvar_flag("sg.box_cull", 1) ? 1 : 0;
    s_verify = lucent_cvar_flag("sg.box_cull_verify", 0) ? 1 : 0;
  }
  return s_enabled;
}

static int native_exact(const CPU *C, unsigned pushes) {
  return enabled() && x87_exact_for_guest(&C->x87, pushes);
}

static void read_floats(uint32_t address, float *out, unsigned count) {
  for (unsigned i = 0; i < count; i++) {
    out[i] = x86_loadf32(address + i * 4u);
  }
}

static uint32_t libigsg_base(void) {
  if (__builtin_expect(!s_libigsg_base, 0)) {
    s_libigsg_base = x86_module_base(LIBIGSG);
    if (!s_libigsg_base) {
      x2_log_error("box_cull: %s is not mapped, yet its box-test override "
                   "ran; not continuing.\n",
                   LIBIGSG);
      abort();
    }
  }
  return s_libigsg_base;
}

/* A linked libIGSg address, where the module was placed. */
static uint32_t libigsg_mapped(uint32_t linked) {
  return libigsg_base() + (linked - LIBIGSG_PREFERRED_BASE);
}

/* The guest body on a copy of the state the override started from, which
   must end where the native answer did: the same EAX (not corners'), ESP,
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
             (ep == CORNERS_EP || guest.reg[kX86pEax] == native_eax);
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
  s_verified[ep == DRIVER_EP ? 2 : ep == CLASSIFY_EP]++;
  const uint64_t total = s_verified[0] + s_verified[1] + s_verified[2];
  if ((total & (total - 1u)) == 0u && (total & 0x5555555555555555ull)) {
    x2_log_info("sg.box_cull_verify: %llu native answers match the guest "
                "body (corners %llu, classify %llu, driver %llu; %llu calls "
                "undecided or recomposing, run as guest body)\n",
                (unsigned long long)total, (unsigned long long)s_verified[0],
                (unsigned long long)s_verified[1],
                (unsigned long long)s_verified[2],
                (unsigned long long)s_undecided);
  }
}

/* The matrix an attribute stack's top names, found as the driver finds it:
   the stack's index is read through the import slot `slot_linked`. */
static uint32_t attribute_matrix(uint32_t attributes, uint32_t slot_linked) {
  const uint32_t index = RD32(RD32(RD32(libigsg_mapped(slot_linked))) + 8u);
  const uint32_t which = RD32(RD32(attributes + 0x44u) + index * 4u);
  const uint32_t stack =
      RD32(RD32(RD32(attributes + 0x10u) + 0x10u) + which * 4u);
  const uint32_t top = RD32(stack + 0x18u);
  uint32_t entry;
  if ((int32_t)top >= 0) {
    entry = RD32(RD32(stack + 0x10u) + top * 4u);
  } else if (RD32(stack + 8u) != 0u) {
    entry = RD32(RD32(stack + 0x10u) + RD32(stack + 8u) * 4u - 4u);
  } else {
    entry = RD32(stack + 0x14u);
  }
  return entry + 0xcu;
}

/* Each function's native answer: 1 when it answered the call, 0 when the
   guest body has to. Verify mode checks every answer against the guest body,
   so a leaf, which may not run guest code, declines while it is on. */
static int verifying(void) {
  (void)enabled();
  return s_verify;
}

static int driver_native(CPU *C) {
  if (!native_exact(C, CORNERS_PUSHES)) {
    return 0;
  }
  const uint32_t esp = C->reg[kX86pEsp];
  const uint32_t traversal = RD32(esp + 4u);
  const uint32_t box = RD32(esp + 8u);
  const uint32_t attributes = RD32(traversal + TRAVERSAL_ATTRIBUTES);
  if (RD32(traversal + TRAVERSAL_COMPOSED_VIEW) !=
          attribute_matrix(attributes, DRIVER_VIEW_INDEX_SLOT_LINKED) ||
      RD32(traversal + TRAVERSAL_COMPOSED_PROJECTION) !=
          attribute_matrix(attributes, DRIVER_PROJECTION_INDEX_SLOT_LINKED)) {
    s_undecided += (uint64_t)s_verify;
    return 0;
  }
  float bounds[6];
  float extent[3];
  float matrix[16];
  float corners[BOX_CULL_CORNER_FLOATS];
  read_floats(box + BOX_BOUNDS, bounds, 6u);
  read_floats(traversal + TRAVERSAL_COMPOSITE, matrix, 16u);
  box_cull_extent(extent, bounds);
  box_cull_corners(corners, bounds, extent, matrix,
                   x86_loadf32(libigsg_mapped(CORNERS_ZERO_LINKED)));
  const BoxCullVerdict verdict = box_cull_classify(corners);
  if (verdict == kBoxCullUndecided) {
    s_undecided += (uint64_t)s_verify;
    return 0;
  }
  if (s_verify) {
    verify_or_abort(C, DRIVER_EP, (uint32_t)verdict, 0u, NULL, 0u);
  }
  C->reg[kX86pEax] = (uint32_t)verdict;
  C->reg[kX86pEsp] = esp + 4u;
  return 1;
}

static int corners_native(CPU *C) {
  if (!native_exact(C, CORNERS_PUSHES)) {
    return 0;
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
                   x86_loadf32(libigsg_mapped(CORNERS_ZERO_LINKED)));
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
  return 1;
}

static int classify_native(CPU *C) {
  if (!native_exact(C, CLASSIFY_PUSHES)) {
    return 0;
  }
  const uint32_t esp = C->reg[kX86pEsp];
  float corners[BOX_CULL_CORNER_FLOATS];
  read_floats(RD32(esp + 4u), corners, BOX_CULL_CORNER_FLOATS);
  const BoxCullVerdict verdict = box_cull_classify(corners);
  if (verdict == kBoxCullUndecided) {
    s_undecided += (uint64_t)s_verify;
    return 0;
  }
  if (s_verify) {
    verify_or_abort(C, CLASSIFY_EP, (uint32_t)verdict, 0u, NULL, 0u);
  }
  C->reg[kX86pEax] = (uint32_t)verdict;
  C->reg[kX86pEsp] = esp + 4u;
  return 1;
}

static int driver_leaf(CPU *C) { return !verifying() && driver_native(C); }
static int corners_leaf(CPU *C) { return !verifying() && corners_native(C); }
static int classify_leaf(CPU *C) { return !verifying() && classify_native(C); }

void x2_override_10047470(CPU *C) {
  if (!driver_native(C)) {
    x86_guest_body(C, LIBIGSG, DRIVER_EP);
  }
}

void x2_override_10047570(CPU *C) {
  if (!corners_native(C)) {
    x86_guest_body(C, LIBIGSG, CORNERS_EP);
  }
}

void x2_override_100478e0(CPU *C) {
  if (!classify_native(C)) {
    x86_guest_body(C, LIBIGSG, CLASSIFY_EP);
  }
}

__attribute__((constructor)) static void box_cull_register_overrides(void) {
  x86_register_override("libIGSg.dll", DRIVER_EP, x2_override_10047470);
  x86_register_override("libIGSg.dll", CORNERS_EP, x2_override_10047570);
  x86_register_override("libIGSg.dll", CLASSIFY_EP, x2_override_100478e0);
  x86_register_override_leaf("libIGSg.dll", DRIVER_EP, driver_leaf);
  x86_register_override_leaf("libIGSg.dll", CORNERS_EP, corners_leaf);
  x86_register_override_leaf("libIGSg.dll", CLASSIFY_EP, classify_leaf);
}
