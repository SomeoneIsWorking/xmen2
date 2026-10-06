/*
 * Unit tests for XMen2.exe!0x005840a0 (CDxImmediateBuilder::addVertex).
 */
#include "vertex_builder.h"

#include "guest_memory.h"
#include "override_leaf.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

static unsigned failures;

int native_stubs_registered(const char *module, uint32_t linked_ep);
x86_override_leaf_fn native_stubs_leaf(const char *module, uint32_t linked_ep);

/* The override and its leaf share one append; every case runs through each,
   and the leaf must complete every call, since verification is off. */
static int s_use_leaf;

static void add_vertex(CPU *c) {
  if (!s_use_leaf) {
    x2_override_005840a0(c);
  } else if (!x2_vertex_builder_leaf(c)) {
    fprintf(stderr, "the leaf declined with verification off\n");
    failures++;
  }
}

int vtx_builder_verifying(void) { return 0; }

void vtx_builder_verify_begin(VtxBuilderVerify *v, uint32_t self) {
  (void)self;
  memset(v, 0, sizeof *v);
}
void vtx_builder_verify_end(const CPU *C, VtxBuilderVerify *v, uint32_t self) {
  (void)C;
  (void)v;
  (void)self;
}

enum {
  ARENA = 0x30000000u,
  ARENA_SIZE = 0x00100000u,
  SELF = ARENA + 0x100u,
  STACK = ARENA + 0x40000u,
  POS_SRC = ARENA + 0x1000u,
  UV_SRC = ARENA + 0x1100u,
  POS_DST = ARENA + 0x2000u,
  COL_DST = ARENA + 0x3000u,
  UV_DST = ARENA + 0x4000u,
};

static void setup_builder(uint32_t cap, uint32_t c14, uint32_t count,
                          int32_t has_uv) {
  WR32(SELF + 0x0cu, cap);
  WR32(SELF + 0x14u, c14);
  WR32(SELF + 0x20u, count);
  WR32(SELF + 0x24u, (uint32_t)has_uv);
  WR32(SELF + 0x28u, POS_DST);
  WR32(SELF + 0x2cu, COL_DST);
  WR32(SELF + 0x30u, UV_DST);
  WR32(SELF + 0x48u, 12u); /* stride pos */
  WR32(SELF + 0x64u, 4u);  /* stride col */
  WR32(SELF + 0x80u, 8u);  /* stride uv */
}

static void test_add_vertex_with_uv(void) {
  setup_builder(100u, 0u, 0u, 1);

  WRF32(POS_SRC + 0u, 1.0f);
  WRF32(POS_SRC + 4u, 2.0f);
  WRF32(POS_SRC + 8u, 3.0f);

  WRF32(UV_SRC + 0u, 0.25f);
  WRF32(UV_SRC + 4u, 0.75f);

  CPU c;
  memset(&c, 0, sizeof c);
  c.reg[kX86pEcx] = SELF;
  c.reg[kX86pEsp] = STACK;
  WR32(STACK + 0u, 0xcafef00du); /* retaddr */
  WR32(STACK + 4u, POS_SRC);
  WR32(STACK + 8u, UV_SRC);
  WR32(STACK + 12u, 0xff00ff00u); /* col */

  add_vertex(&c);

  if (c.reg[kX86pEsp] != STACK + 16u) {
    fprintf(stderr, "esp = %08x, want %08x (ret 0xc)\n", c.reg[kX86pEsp],
            STACK + 16u);
    failures++;
  }
  if (RD32(SELF + 0x20u) != 1u) {
    fprintf(stderr, "count = %u, want 1\n", RD32(SELF + 0x20u));
    failures++;
  }
  if (RD32(POS_DST + 0u) != RD32(POS_SRC + 0u) ||
      RD32(POS_DST + 4u) != RD32(POS_SRC + 4u) ||
      RD32(POS_DST + 8u) != RD32(POS_SRC + 8u)) {
    fprintf(stderr, "position mismatch\n");
    failures++;
  }
  if (RD32(UV_DST + 0u) != RD32(UV_SRC + 0u) ||
      RD32(UV_DST + 4u) != RD32(UV_SRC + 4u)) {
    fprintf(stderr, "uv mismatch\n");
    failures++;
  }
  if (RD32(COL_DST) != 0xff00ff00u) {
    fprintf(stderr, "col mismatch: got %08x\n", RD32(COL_DST));
    failures++;
  }
  if (RD32(SELF + 0x28u) != POS_DST + 12u) {
    fprintf(stderr, "dst_pos pointer did not advance by stride\n");
    failures++;
  }
  if (RD32(SELF + 0x2cu) != COL_DST + 4u) {
    fprintf(stderr, "dst_col pointer did not advance by stride\n");
    failures++;
  }
  if (RD32(SELF + 0x30u) != UV_DST + 8u) {
    fprintf(stderr, "dst_uv pointer did not advance by stride\n");
    failures++;
  }
}

static void test_add_vertex_no_uv(void) {
  setup_builder(100u, 0u, 5u, 0); /* has_uv = 0 */

  WR32(UV_DST, 0xdeadbeefu);

  CPU c;
  memset(&c, 0, sizeof c);
  c.reg[kX86pEcx] = SELF;
  c.reg[kX86pEsp] = STACK;
  WR32(STACK + 0u, 0xcafef00du);
  WR32(STACK + 4u, POS_SRC);
  WR32(STACK + 8u, UV_SRC);
  WR32(STACK + 12u, 0x12345678u);

  add_vertex(&c);

  if (RD32(SELF + 0x20u) != 6u) {
    fprintf(stderr, "count = %u, want 6\n", RD32(SELF + 0x20u));
    failures++;
  }
  if (RD32(UV_DST) != 0xdeadbeefu) {
    fprintf(stderr, "uv was written when has_uv == 0\n");
    failures++;
  }
  if (RD32(SELF + 0x30u) != UV_DST) {
    fprintf(stderr, "dst_uv advanced when has_uv == 0\n");
    failures++;
  }
}

static void test_capacity_limit(void) {
  setup_builder(10u, 5u, 4u, 1); /* 5 + 4 + 1 = 10 >= cap(10) -> full! */

  CPU c;
  memset(&c, 0, sizeof c);
  c.reg[kX86pEcx] = SELF;
  c.reg[kX86pEsp] = STACK;
  WR32(STACK + 0u, 0xcafef00du);
  WR32(STACK + 4u, POS_SRC);
  WR32(STACK + 8u, UV_SRC);
  WR32(STACK + 12u, 0x12345678u);

  add_vertex(&c);

  if (c.reg[kX86pEsp] != STACK + 16u) {
    fprintf(stderr, "esp not cleaned on cap limit\n");
    failures++;
  }
  if (RD32(SELF + 0x20u) != 4u) {
    fprintf(stderr, "count changed on cap limit\n");
    failures++;
  }
  if (RD32(SELF + 0x28u) != POS_DST) {
    fprintf(stderr, "dst_pos advanced on cap limit\n");
    failures++;
  }
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA, ARENA_SIZE, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr, "could not map the test arena\n");
    return 1;
  }

  for (s_use_leaf = 0; s_use_leaf < 2; s_use_leaf++) {
    test_add_vertex_with_uv();
    test_add_vertex_no_uv();
    test_capacity_limit();
  }
  if (native_stubs_leaf("XMen2.exe", 0x005840a0u) != x2_vertex_builder_leaf) {
    fprintf(stderr, "constructor did not register the leaf\n");
    failures++;
  }

  if (!native_stubs_registered("XMen2.exe", 0x005840a0u)) {
    fprintf(stderr, "constructor did not register 0x005840a0\n");
    failures++;
  }

  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("vertex_builder: ok");
  return 0;
}
