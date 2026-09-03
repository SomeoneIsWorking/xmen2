/*
 * Native IMA ADPCM decode for XMen2.exe's statically-linked decoders.
 *
 * 0x00616770 is mono, 0x00616880 is stereo. Both walk a packed nibble stream,
 * and for each nibble: read step = stepTable[stepIndex] (the pre-update step),
 * advance stepIndex by indexTable[nibble & 7] clamped to 0..88, form
 * diff = step>>3 (+ step, + step>>1, + step>>2 for bits 2,1,0), add or subtract
 * it from the predictor per bit 3, clamp the predictor to int16, emit it. That
 * is textbook IMA ADPCM and the tables at 0x006e95a8 / 0x006e9588 are the
 * canonical ones, verified byte-for-byte against the image.
 *
 * Left to the JIT the bodies translate and each instruction runs through the
 * per-instruction interpreter helper; the block profile (issue #141) put the
 * two loops at ~6% of in-game guest wall time. These overrides run the decode
 * as native code instead. `audio.adpcm_verify` (audio_adpcm_verify.c) is the
 * differential gate against the guest's own body.
 */
#include "audio_adpcm.h"

#include "x86rt_native.h"

#include <assert.h>
#include <stdint.h>

/* The 89-entry IMA step-size table (0x006e95a8) and the 8-entry index table
   (0x006e9588), read straight out of the retail image. */
static const int32_t kStep[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int32_t kIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

int32_t ima_adpcm_step(uint32_t nibble, int32_t *predictor,
                       int32_t *step_index) {
  assert(*step_index >= 0 && *step_index <= 88);
  const int32_t step = kStep[*step_index];
  const uint32_t mag = nibble & 7u;

  const int32_t advanced = *step_index + kIndex[mag];
  *step_index = advanced < 0 ? 0 : advanced > 88 ? 88 : advanced;

  int32_t diff = step >> 3;
  if (mag & 4u)
    diff += step;
  if (mag & 2u)
    diff += step >> 1;
  if (mag & 1u)
    diff += step >> 2;

  int32_t next = *predictor + ((nibble & 8u) ? -diff : diff);
  if (next > 32767)
    next = 32767;
  else if (next < -32768)
    next = -32768;
  *predictor = next;
  return next;
}

/* ---- the guest bodies, natively -------------------------------------- */

static void decode_mono(uint32_t out, uint32_t in, int32_t count,
                        int32_t *predictor, int32_t *step_index) {
  for (int32_t i = 0; i < count; i++) {
    const uint32_t byte = RD8(in + (uint32_t)(i >> 1));
    const uint32_t nibble = (i & 1) ? (byte >> 4) : (byte & 0x0fu);
    ima_adpcm_step(nibble, predictor, step_index);
    WR16(out + (uint32_t)(i * 2), (uint16_t)*predictor);
  }
}

static void decode_stereo(uint32_t out, uint32_t in, int32_t frames,
                          int32_t *predictor, int32_t *step_index) {
  for (int32_t i = 0; i < frames; i++) {
    const uint32_t byte = RD8(in + (uint32_t)i);
    ima_adpcm_step(byte & 0x0fu, &predictor[0], &step_index[0]);
    WR16(out + (uint32_t)(i * 4), (uint16_t)predictor[0]);
    ima_adpcm_step(byte >> 4, &predictor[1], &step_index[1]);
    WR16(out + (uint32_t)(i * 4 + 2), (uint16_t)predictor[1]);
  }
}

/* ---- overrides ------------------------------------------------------- */

void x2_override_00616770(CPU *C) {
  const uint32_t out = RD32(C->esp + 4u);
  const uint32_t in = RD32(C->esp + 8u);
  const int32_t count = (int32_t)RD32(C->esp + 12u);
  const uint32_t pp = RD32(C->esp + 16u);
  const uint32_t ip = RD32(C->esp + 20u);
  const int32_t pred0 = (int32_t)RD32(pp);
  const int32_t idx0 = (int32_t)RD32(ip);

  int32_t predictor = pred0;
  int32_t step_index = idx0;
  decode_mono(out, in, count, &predictor, &step_index);
  WR32(pp, (uint32_t)predictor);
  WR32(ip, (uint32_t)step_index);
  C->eax = (uint32_t)count;

  audio_adpcm_verify_or_abort(C, 0x00616770u, out,
                              count > 0 ? (uint32_t)count * 2u : 0u, pp, ip,
                              &pred0, &idx0, &predictor, &step_index, 1);

  C->esp += 4u;
}

void x2_override_00616880(CPU *C) {
  const uint32_t out = RD32(C->esp + 4u);
  const uint32_t in = RD32(C->esp + 8u);
  const int32_t frames = (int32_t)RD32(C->esp + 12u);
  const uint32_t pp = RD32(C->esp + 16u);
  const uint32_t ip = RD32(C->esp + 20u);
  const int32_t pred0[2] = {(int32_t)RD32(pp), (int32_t)RD32(pp + 4u)};
  const int32_t idx0[2] = {(int32_t)RD32(ip), (int32_t)RD32(ip + 4u)};

  int32_t predictor[2] = {pred0[0], pred0[1]};
  int32_t step_index[2] = {idx0[0], idx0[1]};
  decode_stereo(out, in, frames, predictor, step_index);
  WR32(pp, (uint32_t)predictor[0]);
  WR32(pp + 4u, (uint32_t)predictor[1]);
  WR32(ip, (uint32_t)step_index[0]);
  WR32(ip + 4u, (uint32_t)step_index[1]);
  C->eax = out + (frames > 0 ? (uint32_t)frames * 4u : 0u);

  audio_adpcm_verify_or_abort(C, 0x00616880u, out,
                              frames > 0 ? (uint32_t)frames * 4u : 0u, pp, ip,
                              pred0, idx0, predictor, step_index, 2);

  C->esp += 4u;
}

__attribute__((constructor)) static void audio_adpcm_register_overrides(void) {
  x86_register_override("XMen2.exe", 0x00616770u, x2_override_00616770);
  x86_register_override("XMen2.exe", 0x00616880u, x2_override_00616880);
}
