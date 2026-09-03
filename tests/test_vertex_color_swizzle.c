/*
 * Exercises x2_override_10046ce0 through real guest memory, checked against an
 * independent byte-level reference for the channel swap -- not a copy of the
 * code under test.
 */
#include "vertex_color_swizzle.h"

#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

/* The verify gate needs the engine; this test exercises the swap path only. */
void vtx_swizzle_verify_begin(VtxSwizzleVerify *v, uint32_t self,
                              uint32_t desc) {
  (void)self;
  (void)desc;
  memset(v, 0, sizeof *v);
}
void vtx_swizzle_verify_end(const CPU *C, VtxSwizzleVerify *v, uint32_t self,
                            uint32_t desc, uint32_t flags) {
  (void)C;
  (void)v;
  (void)self;
  (void)desc;
  (void)flags;
}

enum {
  ARENA = 0x30000000u,
  ARENA_SIZE = 0x00100000u,
  SELF = ARENA + 0x100u,
  BUFINFO = ARENA + 0x200u,
  DESC = ARENA + 0x300u,
  VBUF = ARENA + 0x1000u,
  STACK = ARENA + 0x40000u,
  STRIDE = 0x20u,
  COLOFF = 0x10u,
};

static unsigned failures;

/* Independent reference: for each vertex in [start, start+count), take the four
   colour bytes and swap the first and the third. */
static void ref_swap(uint8_t *buf, uint32_t stride, uint32_t coloff,
                     uint32_t start, uint32_t count) {
  for (uint32_t i = start; i < start + count; i++) {
    uint8_t *c = buf + i * stride + coloff;
    uint8_t t = c[0];
    c[0] = c[2];
    c[2] = t;
  }
}

static void run_override(uint32_t type, uint32_t start, uint32_t count,
                         uint32_t flags) {
  CPU c;
  memset(&c, 0, sizeof c);
  c.ecx = SELF;
  c.esp = STACK;
  WR32(STACK + 0u, 0xcafef00du); /* return address */
  WR32(STACK + 4u, DESC);
  WR32(STACK + 8u, flags);

  WR32(SELF + 0x08u, BUFINFO);
  WR32(BUFINFO + 0x50u, VBUF);
  WR8(SELF + 0x38u, STRIDE);
  WR8(SELF + 0x3bu, COLOFF);
  WR8(SELF + 0x60u, 0x10u);
  WR8(SELF + 0x68u, 0x07u);

  WR32(DESC + 0x04u, type);
  WR32(DESC + 0x08u, start);
  WR32(DESC + 0x0cu, count);

  x2_override_10046ce0(&c);

  if (c.esp != STACK + 12u) {
    fprintf(stderr, "esp left at %08x, want %08x (ret 8)\n", c.esp,
            STACK + 12u);
    failures++;
  }
}

static void seed_vbuf(uint8_t *shadow, uint32_t nverts) {
  for (uint32_t i = 0; i < nverts * STRIDE; i++) {
    uint8_t b = (uint8_t)(0x11u * i + 0x03u);
    shadow[i] = b;
    WR8(VBUF + i, b);
  }
}

static void check_vbuf(const uint8_t *shadow, uint32_t nverts,
                       const char *tag) {
  for (uint32_t i = 0; i < nverts * STRIDE; i++)
    if ((uint8_t)RD8(VBUF + i) != shadow[i]) {
      fprintf(stderr, "%s: vbuf byte %u = %02x, want %02x\n", tag, i,
              (uint8_t)RD8(VBUF + i), shadow[i]);
      failures++;
      return;
    }
}

static void test_colour_range(void) {
  uint8_t shadow[16 * STRIDE];
  seed_vbuf(shadow, 16);

  run_override(2u, 3u, 9u, 0u); /* type 2, vertices 3..11, flags 0 */
  ref_swap(shadow, STRIDE, COLOFF, 3u, 9u);
  check_vbuf(shadow, 16, "colour range");

  if ((uint8_t)RD8(SELF + 0x60u) != 0x11u) { /* 0x10 | 1 */
    fprintf(stderr, "dirty byte = %02x, want 11\n", (uint8_t)RD8(SELF + 0x60u));
    failures++;
  }
  if ((uint8_t)RD8(SELF + 0x68u) != 0x06u) { /* 0x07 - 1 */
    fprintf(stderr, "lock byte = %02x, want 06\n", (uint8_t)RD8(SELF + 0x68u));
    failures++;
  }
}

static void test_flag_bits(void) {
  uint8_t shadow[8 * STRIDE];
  seed_vbuf(shadow, 8);

  run_override(2u, 0u, 8u, 2u); /* flags bit1 -> dirty |= 2 */
  ref_swap(shadow, STRIDE, COLOFF, 0u, 8u);
  check_vbuf(shadow, 8, "flag bit1");
  if ((uint8_t)RD8(SELF + 0x60u) != 0x12u) {
    fprintf(stderr, "bit1 dirty = %02x, want 12\n", (uint8_t)RD8(SELF + 0x60u));
    failures++;
  }

  seed_vbuf(shadow, 8);
  run_override(2u, 0u, 8u, 1u); /* flags bit0 -> only lock-- */
  ref_swap(shadow, STRIDE, COLOFF, 0u, 8u);
  check_vbuf(shadow, 8, "flag bit0");
  if ((uint8_t)RD8(SELF + 0x60u) != 0x10u) {
    fprintf(stderr, "bit0 dirty changed to %02x, want 10\n",
            (uint8_t)RD8(SELF + 0x60u));
    failures++;
  }
  if ((uint8_t)RD8(SELF + 0x68u) != 0x06u) {
    fprintf(stderr, "bit0 lock = %02x, want 06\n", (uint8_t)RD8(SELF + 0x68u));
    failures++;
  }
}

static void test_non_colour_type(void) {
  uint8_t shadow[4 * STRIDE];
  seed_vbuf(shadow, 4);

  run_override(1u, 0u, 4u, 0u); /* type != 2: no swap, flags still update */
  check_vbuf(shadow, 4, "non-colour type"); /* unchanged */
  if ((uint8_t)RD8(SELF + 0x68u) != 0x06u) {
    fprintf(stderr, "non-colour lock = %02x, want 06\n",
            (uint8_t)RD8(SELF + 0x68u));
    failures++;
  }
}

static void test_word_helper(void) {
  /* 0xAABBCCDD -> keep BB,DD; swap AA<->CC : 0xAADDCCBB... check by bytes */
  uint32_t w = 0xaabbccddu;
  uint32_t got = vtx_color_swizzle_word(w);
  uint32_t want = (w & 0xff00ff00u) | ((w & 0xffu) << 16) | ((w >> 16) & 0xffu);
  if (got != want) {
    fprintf(stderr, "swizzle_word(%08x) = %08x, want %08x\n", w, got, want);
    failures++;
  }
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA, ARENA_SIZE, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr, "could not map the test arena\n");
    return 1;
  }

  test_word_helper();
  test_colour_range();
  test_flag_bits();
  test_non_colour_type();

  if (!native_stubs_registered("libIGGfx.dll", 0x10046ce0u)) {
    fprintf(stderr, "the constructor did not register the override\n");
    failures++;
  }

  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("vertex_color_swizzle: ok");
  return 0;
}
