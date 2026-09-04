/*
 * Unit tests for XMen2.exe!0x00594500 (per-frame audio channel poll)
 * native override.
 *
 * Drives the override over a synthetic 24-entry channel table with stub
 * DirectSound helpers, covering every branch of the retail body:
 * state filtering, the still-playing skip, owned-buffer release + slot
 * teardown, the non-owning drop to state 1, and the recursion guard.
 */
#include "audio_channel_poll.h"

#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int native_stubs_registered(const char *module, uint32_t linked_ep);

/* --- stubs for the override's collaborators ------------------------------ */

int audio_channel_poll_verify(CPU *C) {
  (void)C;
  return 0; /* gate disabled in the unit test */
}

void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  (void)C;
  (void)module;
  (void)linked_ep;
}

/* Stub DirectSound: a tiny table keyed by guest pointer. */
#define MAX_BUFS 8
static struct {
  uint32_t guest;
  int playing;
  unsigned releases;
} g_bufs[MAX_BUFS];
static unsigned g_release_calls;

static void buf_reset(void) {
  memset(g_bufs, 0, sizeof g_bufs);
  g_release_calls = 0;
}

static void buf_add(uint32_t guest, int playing) {
  for (int i = 0; i < MAX_BUFS; ++i) {
    if (!g_bufs[i].guest) {
      g_bufs[i].guest = guest;
      g_bufs[i].playing = playing;
      return;
    }
  }
}

int dsound_buffer_is_playing(uint32_t guest) {
  for (int i = 0; i < MAX_BUFS; ++i)
    if (g_bufs[i].guest == guest)
      return g_bufs[i].playing;
  return 0;
}

unsigned dsound_buffer_release_guest(uint32_t guest) {
  g_release_calls++;
  for (int i = 0; i < MAX_BUFS; ++i)
    if (g_bufs[i].guest == guest)
      g_bufs[i].releases++;
  return 0;
}

/* --- layout mirrors audio_channel_poll.c ------------------------------- */

enum {
  DATA_BASE = 0x00804000u,
  DATA_SIZE = 0x00002000u,
  GUARD = 0x0080431cu,
  COUNTER = 0x00804330u,
  CHAN_BASE = 0x00804198u,
  CHAN_STRIDE = 0x10u,
  SLOT_LO = 0x00804098u,
  SLOT_HI = 0x00804099u,
  CH_STATE = 0x00u,
  CH_FLAGS = 0x01u,
  CH_SLOT = 0x03u,
  CH_OBJ = 0x0cu,
};

static uint32_t chan(int i) { return CHAN_BASE + (uint32_t)i * CHAN_STRIDE; }

static void set_channel(int i, uint8_t state, uint8_t flags, uint8_t slot,
                        uint32_t obj) {
  WR8(chan(i) + CH_STATE, state);
  WR8(chan(i) + CH_FLAGS, flags);
  WR8(chan(i) + CH_SLOT, slot);
  WR32(chan(i) + CH_OBJ, obj);
}

static unsigned failures;

#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg);            \
      failures++;                                                              \
    }                                                                          \
  } while (0)

static void clear_data(void) {
  for (uint32_t a = DATA_BASE; a < DATA_BASE + DATA_SIZE; a += 4u)
    WR32(a, 0u);
}

static void test_branches(void) {
  clear_data();
  buf_reset();

  buf_add(0xB0000001u, 1); /* still playing            */
  buf_add(0xB0000002u, 0); /* finished, owned          */
  buf_add(0xB0000003u, 0); /* finished, not owned      */

  set_channel(0, 0, 1, 10, 0xB0000009u); /* state 0 -> ignored     */
  set_channel(1, 2, 1, 11, 0xB0000001u); /* playing -> keep        */
  set_channel(2, 2, 1, 12, 0xB0000002u); /* finished+owned -> free */
  set_channel(3, 2, 0, 13, 0xB0000003u); /* finished+!owned -> 1   */
  set_channel(4, 2, 1, 14, 0u);          /* finished+owned, no obj */

  CPU C;
  memset(&C, 0, sizeof C);
  C.reg[kX86pEsp] = 0x70000000u;
  x2_override_00594500(&C);

  CHECK(C.reg[kX86pEsp] == 0x70000004u,
        "override did not pop the return address");
  CHECK(C.reg[kX86pEax] == 0u, "override eax not 0");
  CHECK(RD32(COUNTER) == 1u, "poll counter not bumped once");

  CHECK(RD8(chan(0) + CH_STATE) == 0u, "state-0 channel touched");

  CHECK(RD8(chan(1) + CH_STATE) == 2u, "playing channel not kept at state 2");
  CHECK(RD32(chan(1) + CH_OBJ) == 0xB0000001u, "playing channel obj cleared");

  CHECK(RD8(chan(2) + CH_STATE) == 0u, "finished owned channel not freed");
  CHECK(RD32(chan(2) + CH_OBJ) == 0u, "finished owned channel obj not cleared");
  CHECK(RD8(SLOT_LO + 2u * 12u) == 0u, "slot-lo[12] not cleared");
  CHECK(RD8(SLOT_HI + 2u * 12u) == 0xffu, "slot-hi[12] not 0xff");
  CHECK(g_release_calls == 1u, "release called wrong number of times");

  CHECK(RD8(chan(3) + CH_STATE) == 1u, "non-owning finished channel not -> 1");
  CHECK(RD32(chan(3) + CH_OBJ) == 0xB0000003u,
        "non-owning channel obj changed");

  CHECK(RD8(chan(4) + CH_STATE) == 0u, "no-obj owned channel not freed");
  CHECK(RD8(SLOT_HI + 2u * 14u) == 0xffu, "slot-hi[14] not 0xff");
}

static void test_guard_no_ops(void) {
  clear_data();
  buf_reset();
  buf_add(0xB0000002u, 0);

  WR32(GUARD, 1u);
  set_channel(2, 2, 1, 12, 0xB0000002u);

  audio_channel_poll_run();

  CHECK(RD32(COUNTER) == 0u, "counter bumped while guard held");
  CHECK(RD8(chan(2) + CH_STATE) == 2u, "channel serviced while guard held");
  CHECK(g_release_calls == 0u, "release called while guard held");
}

static void test_counter_increments(void) {
  clear_data();
  buf_reset();
  audio_channel_poll_run();
  audio_channel_poll_run();
  audio_channel_poll_run();
  CHECK(RD32(COUNTER) == 3u, "counter did not advance once per run");
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(DATA_BASE, DATA_SIZE, PROT_READ | PROT_WRITE) !=
          0) {
    fprintf(stderr, "could not map test data region\n");
    return 1;
  }

  test_branches();
  test_guard_no_ops();
  test_counter_increments();

  if (!native_stubs_registered("XMen2.exe", 0x00594500u)) {
    fprintf(stderr, "constructor did not register 0x00594500\n");
    failures++;
  }

  if (failures) {
    fprintf(stderr, "%u failure(s)\n", failures);
    return 1;
  }
  puts("audio_channel_poll: ok");
  return 0;
}
