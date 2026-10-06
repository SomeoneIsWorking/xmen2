/*
 * Exercises the shipping ADPCM overrides (x2_override_00616770 / _00616880)
 * through real guest memory, checked against an independent IMA reference
 * decoder written from the published algorithm -- not a copy of the code under
 * test.
 */
#include "audio_adpcm.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

/* The differential gate (audio_adpcm_verify.c) needs the engine; this test
   exercises the decode path only, so it stands in with a no-op. */
void audio_adpcm_verify_or_abort(const CPU *C, uint32_t ep, uint32_t out,
                                 uint32_t out_bytes, uint32_t pp, uint32_t ip,
                                 const int32_t *pred0, const int32_t *idx0,
                                 const int32_t *native_pred,
                                 const int32_t *native_idx, int channels) {
  (void)C;
  (void)ep;
  (void)out;
  (void)out_bytes;
  (void)pp;
  (void)ip;
  (void)pred0;
  (void)idx0;
  (void)native_pred;
  (void)native_idx;
  (void)channels;
}

enum {
  ARENA = 0x30000000u,
  ARENA_SIZE = 0x00100000u,
  IN = ARENA + 0x1000u,
  OUT = ARENA + 0x8000u,
  STATE = ARENA + 0x100u,
  STACK = ARENA + 0x40000u,
};

static unsigned failures;

/* Independent IMA ADPCM, from the spec: step table by index, index adjusted by
   {-1,-1,-1,-1,2,4,6,8}, diff = step/8 + step*bit2 + step/2*bit1 + step/4*bit0,
   predictor +/- diff clamped to int16. Deliberately phrased differently from
   the implementation under test. */
static const int ref_step[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static int16_t ref_decode_nibble(unsigned n, int *pred, int *idx) {
  static const int adj[8] = {-1, -1, -1, -1, 2, 4, 6, 8};
  int s = ref_step[*idx];
  int m = (int)(n & 7u);
  long d = s / 8;
  if (m & 4)
    d += s;
  if (m & 2)
    d += s / 2;
  if (m & 1)
    d += s / 4;
  long p = *pred + ((n & 8u) ? -d : d);
  if (p > 32767)
    p = 32767;
  if (p < -32768)
    p = -32768;
  *pred = (int)p;
  *idx += adj[m];
  if (*idx < 0)
    *idx = 0;
  if (*idx > 88)
    *idx = 88;
  return (int16_t)p;
}

static void put_bytes(uint32_t addr, const uint8_t *b, uint32_t n) {
  for (uint32_t i = 0; i < n; i++)
    WR8(addr + i, b[i]);
}

static void call_override(void (*fn)(CPU *), uint32_t out, uint32_t in,
                          int32_t count, uint32_t state) {
  CPU c;
  memset(&c, 0, sizeof c);
  c.reg[kX86pEsp] = STACK;
  WR32(STACK + 0u, 0xdeadbeefu); /* return address */
  WR32(STACK + 4u, out);
  WR32(STACK + 8u, in);
  WR32(STACK + 12u, (uint32_t)count);
  WR32(STACK + 16u, state);      /* predictor pointer */
  WR32(STACK + 20u, state + 8u); /* step-index pointer */
  fn(&c);
  if (c.reg[kX86pEsp] != STACK + 4u) {
    fprintf(stderr,
            "override left esp at %08x, want %08x (cdecl pops the "
            "return address only)\n",
            c.reg[kX86pEsp], STACK + 4u);
    failures++;
  }
}

static void test_mono(void) {
  uint8_t in[24];
  for (unsigned i = 0; i < sizeof in; i++)
    in[i] = (uint8_t)(0x93u * (i + 1)); /* arbitrary but deterministic */
  put_bytes(IN, in, sizeof in);

  const int32_t count = 40;
  WR32(STATE + 0u, 0u);  /* predictor */
  WR32(STATE + 8u, 20u); /* step index */
  call_override(x2_override_00616770, OUT, IN, count, STATE);

  int pred = 0, idx = 20;
  for (int32_t i = 0; i < count; i++) {
    unsigned nib =
        (i & 1) ? (unsigned)(in[i >> 1] >> 4) : (unsigned)(in[i >> 1] & 0x0fu);
    int16_t want = ref_decode_nibble(nib, &pred, &idx);
    int16_t got = (int16_t)RD16(OUT + (uint32_t)(i * 2));
    if (got != want) {
      fprintf(stderr, "mono sample %d: got %d, want %d\n", i, got, want);
      failures++;
    }
  }
  if ((int32_t)RD32(STATE + 0u) != pred || (int32_t)RD32(STATE + 8u) != idx) {
    fprintf(stderr, "mono state: predictor %d/%d, index %d/%d\n",
            (int32_t)RD32(STATE + 0u), pred, (int32_t)RD32(STATE + 8u), idx);
    failures++;
  }
}

static void test_stereo(void) {
  uint8_t in[32];
  for (unsigned i = 0; i < sizeof in; i++)
    in[i] = (uint8_t)(0x2fu * i + 0x11u);
  put_bytes(IN, in, sizeof in);

  const int32_t frames = 24;
  WR32(STATE + 0u, 100);  /* left predictor  */
  WR32(STATE + 4u, -50);  /* right predictor */
  WR32(STATE + 8u, 5u);   /* left index  */
  WR32(STATE + 12u, 60u); /* right index */
  call_override(x2_override_00616880, OUT, IN, frames, STATE);

  int lp = 100, rp = -50, li = 5, ri = 60;
  for (int32_t i = 0; i < frames; i++) {
    int16_t wl = ref_decode_nibble(in[i] & 0x0fu, &lp, &li);
    int16_t wr = ref_decode_nibble((unsigned)in[i] >> 4, &rp, &ri);
    int16_t gl = (int16_t)RD16(OUT + (uint32_t)(i * 4));
    int16_t gr = (int16_t)RD16(OUT + (uint32_t)(i * 4 + 2));
    if (gl != wl || gr != wr) {
      fprintf(stderr, "stereo frame %d: got L%d R%d, want L%d R%d\n", i, gl, gr,
              wl, wr);
      failures++;
    }
  }
  if ((int32_t)RD32(STATE + 0u) != lp || (int32_t)RD32(STATE + 4u) != rp ||
      (int32_t)RD32(STATE + 8u) != li || (int32_t)RD32(STATE + 12u) != ri) {
    fprintf(stderr, "stereo state mismatch\n");
    failures++;
  }
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA, ARENA_SIZE, PROT_READ | PROT_WRITE) != 0) {
    fprintf(stderr, "could not map the test arena\n");
    return 1;
  }

  test_mono();
  test_stereo();

  if (!native_stubs_registered("XMen2.exe", 0x00616770u) ||
      !native_stubs_registered("XMen2.exe", 0x00616880u)) {
    fprintf(stderr, "the constructor did not register both ADPCM overrides\n");
    failures++;
  }

  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("audio_adpcm: ok");
  return 0;
}
