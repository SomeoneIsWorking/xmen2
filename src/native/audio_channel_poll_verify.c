/*
 * `audio.channel_poll_verify` -- differential gate for the audio channel poll
 * native override (XMen2.exe!0x00594500) against the retail guest body.
 *
 * The guest body has host-side effects our snapshot cannot rewind: a finished
 * buffer is Released through dsound.c and its DSBuffer slot is torn down. So
 * this runs the GUEST body first, records the guest-visible memory it wrote,
 * rewinds the guest memory, then runs the native poll. On the rewound state
 * the native poll re-observes the same buffers: an already-released buffer
 * reports "not playing" and its release is a no-op, so the native poll writes
 * the same channel-table and slot-table bytes the guest body did.
 *
 * Compared: every channel's state byte and buffer pointer, the poll counter,
 * and the slot table. Not compared: host DSBuffer bookkeeping (the guest body
 * already settled it). A buffer that finishes in the sub-microsecond gap
 * between the two runs would diverge; that race is accepted for a verify-only
 * mode, as it is for the other override verifiers.
 */
#include "audio_channel_poll_verify.h"
#include "audio_channel_poll.h"

#include "guest_body.h"
#include "x86rt.h"

#include <lucent/cvar_c.h>

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
  V_GUARD = 0x0080431cu,
  V_COUNTER = 0x00804330u,
  V_CHAN_BASE = 0x00804198u,
  V_CHAN_STRIDE = 0x10u,
  V_CHAN_COUNT = 24,
  V_STATE = 0x00u,
  V_OBJ = 0x0cu,
  V_SLOT_BASE = 0x00804098u,
  V_SLOT_BYTES = 0x200u, /* slot index is a byte; 2 bytes per record */
};

typedef struct {
  uint8_t state[V_CHAN_COUNT];
  uint32_t obj[V_CHAN_COUNT];
  uint32_t counter;
  uint8_t slots[V_SLOT_BYTES];
} PollSnapshot;

static int verify_enabled(void) {
  static int cached = -1;
  if (cached < 0)
    cached = lucent_cvar_flag("audio.channel_poll_verify", 0) ? 1 : 0;
  return cached;
}

static void snapshot(PollSnapshot *s) {
  for (int i = 0; i < V_CHAN_COUNT; ++i) {
    const uint32_t rec = V_CHAN_BASE + (uint32_t)i * V_CHAN_STRIDE;
    s->state[i] = (uint8_t)RD8(rec + V_STATE);
    s->obj[i] = RD32(rec + V_OBJ);
  }
  s->counter = RD32(V_COUNTER);
  for (uint32_t b = 0; b < V_SLOT_BYTES; ++b)
    s->slots[b] = (uint8_t)RD8(V_SLOT_BASE + b);
}

static void restore(const PollSnapshot *s) {
  for (int i = 0; i < V_CHAN_COUNT; ++i) {
    const uint32_t rec = V_CHAN_BASE + (uint32_t)i * V_CHAN_STRIDE;
    WR8(rec + V_STATE, s->state[i]);
    WR32(rec + V_OBJ, s->obj[i]);
  }
  WR32(V_COUNTER, s->counter);
  for (uint32_t b = 0; b < V_SLOT_BYTES; ++b)
    WR8(V_SLOT_BASE + b, s->slots[b]);
}

static void compare(const PollSnapshot *g, const PollSnapshot *n) {
  for (int i = 0; i < V_CHAN_COUNT; ++i) {
    if (g->state[i] != n->state[i] || g->obj[i] != n->obj[i]) {
      fprintf(stderr,
              "audio.channel_poll_verify: channel %d diverged -- guest "
              "state=%u obj=0x%08x, native state=%u obj=0x%08x\n",
              i, g->state[i], g->obj[i], n->state[i], n->obj[i]);
      assert(0 && "audio channel poll override diverged from guest body");
    }
  }
  assert(g->counter == n->counter &&
         "audio channel poll counter diverged from guest body");
  assert(memcmp(g->slots, n->slots, V_SLOT_BYTES) == 0 &&
         "audio channel poll slot table diverged from guest body");
}

static unsigned long g_verify_runs;

int audio_channel_poll_verify(struct CPU *C) {
  if (!verify_enabled())
    return 0;

  /* The retail body no-ops while the recursion guard is held; nothing to
     verify, and running the native poll would be a no-op too. */
  if (RD32(V_GUARD) != 0u) {
    audio_channel_poll_run();
    return 1;
  }

  PollSnapshot s0, sg, sn;
  snapshot(&s0);

  CPU guest = *C;
  x86_guest_body(&guest, "XMen2.exe", 0x00594500u);
  snapshot(&sg);

  restore(&s0);
  audio_channel_poll_run();
  snapshot(&sn);

  compare(&sg, &sn);

  if (g_verify_runs++ == 0 || (g_verify_runs % 4096u) == 0u) {
    int active = 0;
    for (int i = 0; i < V_CHAN_COUNT; ++i)
      active += s0.state[i] == 2u;
    fprintf(stderr,
            "audio.channel_poll_verify: run %lu, %d channel(s) awaiting "
            "completion, native poll matches the guest body\n",
            g_verify_runs, active);
  }
  return 1;
}
