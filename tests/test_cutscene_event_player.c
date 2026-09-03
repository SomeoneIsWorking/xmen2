#define _GNU_SOURCE

#include "cutscene_event_player.h"
#include "guest_memory.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

enum {
  ARENA_BASE = 0x33000000u,
  ARENA_SIZE = 0x00400000u,
  OWNER = ARENA_BASE + 0x10000u,
  STACK = ARENA_BASE + ARENA_SIZE - 0x100u,

  CALLBACK_STRIDE = 0x18u,
  ALLOCATED_BITS = 0x6978u,
  LIVE_BITS = 0x7bacu,
  LIVE_COUNT = 0x7c3cu,
  HEAP = 0x7c40u,
  HEAP_COUNT = 0x9f6cu,
  FN_CALLBACK_EXECUTE = 0x004199f0u,
  FN_EVENT_INSERT = 0x004b2b40u,
  FN_EVENT_PUMP = 0x004b2d70u,
  FN_SLOT_FREE = 0x004b2ea0u
};

typedef struct Pair {
  uint32_t deadline_bits;
  uint32_t slot;
} Pair;

static struct Registration {
  const char *module;
  uint32_t ep;
  x86_override_fn fn;
} registered[2];
static unsigned registration_count;
static char calls[4096];
static unsigned call_count;
static unsigned failures;
static int cascade_from = -1;
static uint32_t cascade_to;
static float cascade_deadline;
static uint32_t super_slot;
static float super_deadline;
static int super_mode = 1;
static unsigned super_calls;
static int insertion_is_owned;
static int callback_was_owned;
static uint32_t mapped_exe = 0x00400000u;
static X86Module module = {
    .name = "XMen2.exe", .base = &mapped_exe, .preferred = 0x00400000u};

static void invoke_insert(uint32_t slot, float deadline);

volatile uint32_t x2_write_watch_addr;

void x2_write_watch_fire(uint32_t address, uint32_t value) {
  (void)address;
  (void)value;
  abort();
}

static void fail(const char *message) {
  fprintf(stderr, "FAIL: %s\n", message);
  ++failures;
}

#define CHECK(condition, message)                                              \
  do {                                                                         \
    if (!(condition))                                                          \
      fail(message);                                                           \
  } while (0)

static uint32_t float_bits(float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  return bits;
}

static float pair_deadline(Pair pair) {
  float value;
  memcpy(&value, &pair.deadline_bits, sizeof value);
  return value;
}

static uint32_t entry_address(uint32_t index) {
  return OWNER + HEAP + index * 8u;
}

static Pair read_pair(uint32_t index) {
  Pair pair;
  memcpy(&pair, guest_memory_const_pointer(entry_address(index)), sizeof pair);
  return pair;
}

static void write_pair(uint32_t index, Pair pair) {
  WR32(entry_address(index), pair.deadline_bits);
  WR32(entry_address(index) + 4u, pair.slot);
}

static uint32_t bitmap_address(uint32_t offset, uint32_t slot) {
  return OWNER + offset + (slot / 32u) * 4u;
}

static uint32_t slot_bit(uint32_t slot) { return 1u << (slot % 32u); }

static void mark_slot(uint32_t slot) {
  uint32_t allocated = bitmap_address(ALLOCATED_BITS, slot);
  uint32_t live = bitmap_address(LIVE_BITS, slot);
  WR32(allocated, RD32(allocated) | slot_bit(slot));
  WR32(live, RD32(live) | slot_bit(slot));
}

static void release_slot(uint32_t slot) {
  uint32_t allocated = bitmap_address(ALLOCATED_BITS, slot);
  uint32_t live = bitmap_address(LIVE_BITS, slot);
  WR32(allocated, RD32(allocated) & ~slot_bit(slot));
  WR32(live, RD32(live) & ~slot_bit(slot));
  WR32(OWNER + LIVE_COUNT, RD32(OWNER + LIVE_COUNT) - 1u);
}

static int queue_event(uint32_t slot, float deadline) {
  uint32_t count = RD32(OWNER + HEAP_COUNT);
  uint32_t index;
  Pair pair = {float_bits(deadline), slot};

  if (slot >= CUTSCENE_EVENT_PLAYER_CAPACITY ||
      count >= CUTSCENE_EVENT_PLAYER_CAPACITY ||
      (RD32(bitmap_address(LIVE_BITS, slot)) & slot_bit(slot)) != 0u)
    return 0;
  mark_slot(slot);
  WR32(OWNER + LIVE_COUNT, RD32(OWNER + LIVE_COUNT) + 1u);
  index = count;
  write_pair(index, pair);
  WR32(OWNER + HEAP_COUNT, count + 1u);
  while (index != 0u) {
    uint32_t parent = (index - 1u) / 2u;
    Pair parent_pair = read_pair(parent);
    if (!(pair_deadline(pair) < pair_deadline(parent_pair)))
      break;
    write_pair(index, parent_pair);
    write_pair(parent, pair);
    index = parent;
  }
  return 1;
}

static void reset_owner(void) {
  memset(guest_memory_pointer(OWNER), 0, HEAP_COUNT + 4u);
  calls[0] = '\0';
  call_count = 0;
  cascade_from = -1;
  callback_was_owned = 0;
}

static void set_heap(const float *deadlines, const uint32_t *slots,
                     uint32_t count) {
  uint32_t index;
  reset_owner();
  for (index = 0; index < count; ++index) {
    Pair pair = {float_bits(deadlines[index]), slots[index]};
    write_pair(index, pair);
    mark_slot(slots[index]);
  }
  WR32(OWNER + HEAP_COUNT, count);
  WR32(OWNER + LIVE_COUNT, count);
}

static void note(char event) {
  if (call_count + 1u >= sizeof calls)
    abort();
  calls[call_count++] = event;
  calls[call_count] = '\0';
}

X86Module *x86_modules(void) { return &module; }

int x86_peek(uint32_t address, void *out, size_t size) {
  uint64_t end = (uint64_t)address + size;
  if (address < ARENA_BASE || end > (uint64_t)ARENA_BASE + ARENA_SIZE)
    return 0;
  memcpy(out, guest_memory_const_pointer(address), size);
  return 1;
}

int x86_peek32(uint32_t address, uint32_t *out) {
  return x86_peek(address, out, sizeof *out);
}

void x86_guest_call_args(CPU *cpu, uint32_t target, uint32_t callee_pop_bytes) {
  if (target == FN_CALLBACK_EXECUTE) {
    uint32_t distance = cpu->ecx - OWNER;
    uint32_t slot = distance / CALLBACK_STRIDE;
    CHECK(cpu->ecx >= OWNER && distance % CALLBACK_STRIDE == 0u &&
              slot < CUTSCENE_EVENT_PLAYER_CAPACITY,
          "callback executor received an invalid record");
    callback_was_owned = cutscene_event_player_executing_owned();
    note('E');
    if ((int)slot == cascade_from) {
      invoke_insert(cascade_to, cascade_deadline);
      cascade_from = -1;
    }
  } else if (target == FN_SLOT_FREE) {
    uint32_t slot = RD32(cpu->esp);
    CHECK(cpu->ecx == OWNER && slot < CUTSCENE_EVENT_PLAYER_CAPACITY,
          "slot free received the wrong owner or slot");
    release_slot(slot);
    note('F');
  } else {
    fail("event player called an unexpected guest function");
  }
  cpu->esp += callee_pop_bytes;
}

void x86_register_override(const char *module, uint32_t ep,
                           x86_override_fn fn) {
  if (registration_count >= 2u)
    abort();
  registered[registration_count].module = module;
  registered[registration_count].ep = ep;
  registered[registration_count].fn = fn;
  ++registration_count;
}

static x86_override_fn registered_fn(uint32_t ep) {
  unsigned index;
  for (index = 0; index < registration_count; ++index)
    if (registered[index].ep == ep)
      return registered[index].fn;
  return NULL;
}

static void guest_body_004b2b40(CPU *cpu) {
  ++super_calls;
  if (super_mode >= 1)
    CHECK(queue_event(super_slot, super_deadline),
          "insertion super-call could not queue its event");
  if (super_mode >= 2)
    CHECK(queue_event(super_slot + 1u, super_deadline + 1.0f),
          "corrupt insertion super-call could not queue second event");
  cpu->esp += 12u;
}

static CPU fresh_cpu(void) {
  CPU cpu;
  memset(&cpu, 0, sizeof cpu);
  cpu.esp = STACK;
  return cpu;
}

static int owns_current_insertion(const CPU *cpu, void *opaque) {
  (void)cpu;
  (void)opaque;
  return insertion_is_owned;
}

static void invoke_insert(uint32_t slot, float deadline) {
  CPU cpu = fresh_cpu();
  x86_override_fn insertion = registered_fn(FN_EVENT_INSERT);
  super_slot = slot;
  super_deadline = deadline;
  WR32(cpu.esp, 0xabc10000u + slot);
  WR32(cpu.esp + 4u, 0x11111111u);
  WR32(cpu.esp + 8u, float_bits(deadline));
  cpu.ecx = OWNER;
  insertion(&cpu);
  CHECK(cpu.esp == STACK + 12u, "insertion override did not reproduce RET 8");
}

static void capture_owner(void) {
  CPU cpu = fresh_cpu();
  cpu.ecx = OWNER;
  WR32(cpu.esp, 0xabcdef01u);
  WR32(cpu.esp + 4u, float_bits(-1000.0f));
  registered_fn(FN_EVENT_PUMP)(&cpu);
  CHECK(cpu.esp == STACK + 8u, "ordinary override did not reproduce RET 4");
  CHECK(cutscene_event_player_captured_owner() == OWNER,
        "ordinary pump did not capture its validated owner");
}

static void test_early_insertion_binding(void) {
  CutsceneEventOwnershipWindow window;
  uint32_t slot = UINT32_MAX;

  reset_owner();
  memset(&window, 0xa5, sizeof window);
  window.active = 0u;
  insertion_is_owned = 1;
  CHECK(cutscene_event_player_captured_owner() == 0u,
        "owner was captured before any pump or insertion");
  CHECK(cutscene_event_player_watch_insertions(&window, owns_current_insertion,
                                               NULL) == 1,
        "early insertion watch refused before owner capture");
  CHECK(!window.active,
        "early insertion watch guessed an owner before observation");
  invoke_insert(3u, 7.0f);
  CHECK(window.active && window.owner == OWNER &&
            cutscene_event_player_captured_owner() == OWNER,
        "first validated insertion did not bind the pending window");
  CHECK(cutscene_event_player_window_claim_new(&window) == 1,
        "first owned insertion was not reported");
  CHECK(cutscene_event_player_next_owned(&window, &slot) == 1 && slot == 3u,
        "first owned insertion was not tagged at the insertion seam");
  cutscene_event_player_unwatch_insertions(&window);
}

static void test_strict_ordinary_pump(void) {
  static const float deadlines[] = {4.0f, 5.0f};
  static const uint32_t slots[] = {2u, 3u};
  CPU cpu;

  set_heap(deadlines, slots, 2u);
  cpu = fresh_cpu();
  cpu.ecx = OWNER;
  WR32(cpu.esp, 0xabcdef02u);
  WR32(cpu.esp + 4u, float_bits(5.0f));
  registered_fn(FN_EVENT_PUMP)(&cpu);
  CHECK(RD32(OWNER + HEAP_COUNT) == 1u,
        "ordinary pump did not consume exactly the strictly due event");
  CHECK(read_pair(0u).slot == 3u &&
            read_pair(0u).deadline_bits == float_bits(5.0f),
        "deadline == now did not remain queued");
  CHECK(!strcmp(calls, "EF"),
        "ordinary pump did not execute then free the due callback");

  set_heap(deadlines, slots, 2u);
  cascade_from = 2;
  cascade_to = 8u;
  cascade_deadline = 8.0f;
  cpu = fresh_cpu();
  cpu.ecx = OWNER;
  WR32(cpu.esp, 0xabcdef02u);
  WR32(cpu.esp + 4u, float_bits(5.0f));
  registered_fn(FN_EVENT_PUMP)(&cpu);
  CHECK(
      RD32(OWNER + HEAP_COUNT) == 2u,
      "ordinary callback insertion was refused while its parent was inflight");
  CHECK((read_pair(0u).slot == 3u && read_pair(1u).slot == 8u) ||
            (read_pair(0u).slot == 8u && read_pair(1u).slot == 3u),
        "ordinary callback child was not retained with the equality event");
  CHECK(!strcmp(calls, "EF"),
        "ordinary callback insertion changed dispatch-before-free order");
}

static void test_window_claim_selection_and_followup(void) {
  static const float deadlines[] = {20.0f};
  static const uint32_t slots[] = {9u};
  CutsceneEventOwnershipWindow window;
  uint8_t before[HEAP_COUNT + 4u];
  uint32_t slot = UINT32_MAX;
  CPU cpu = fresh_cpu();

  set_heap(deadlines, slots, 1u);
  capture_owner();
  CHECK(cutscene_event_player_window_begin(&window) == 1,
        "ownership window refused a valid captured owner");
  insertion_is_owned = 1;
  CHECK(cutscene_event_player_watch_insertions(&window, owns_current_insertion,
                                               NULL) == 1,
        "could not watch cutscene insertion seam");
  invoke_insert(4u, 6.0f);
  invoke_insert(5u, 3.0f);
  CHECK(cutscene_event_player_window_claim_new(&window) == 2,
        "window did not report inserted owned slots");
  insertion_is_owned = 0;
  invoke_insert(6u, 1.0f);
  CHECK(cutscene_event_player_window_claim_new(&window) == 0,
        "foreign insertion was claimed by the cutscene window");
  memcpy(before, guest_memory_const_pointer(OWNER), sizeof before);
  CHECK(cutscene_event_player_next_owned(&window, &slot) == 1 && slot == 5u,
        "read-only selection did not choose the earliest owned callback");
  CHECK(!memcmp(before, guest_memory_const_pointer(OWNER), sizeof before),
        "read-only owned selection changed guest state");

  cascade_from = 5;
  cascade_to = 8u;
  cascade_deadline = 2.0f;
  CHECK(cutscene_event_player_step_owned_slot(&cpu, &window, 5u) ==
            CUTSCENE_EVENT_PLAYER_STEP_RAN,
        "exact owned callback step refused");
  CHECK(!strcmp(calls, "EF"),
        "exact step did not execute then free its callback");
  CHECK(callback_was_owned,
        "owned callback execution did not publish its causal scope");
  CHECK(!cutscene_event_player_executing_owned(),
        "owned callback scope leaked past the callback");
  CHECK(cutscene_event_player_next_owned(&window, &slot) == 1 && slot == 4u,
        "entity callback child displaced the remaining script-posted event");
  CHECK(cutscene_event_player_step_owned_slot(&cpu, &window, 8u) ==
            CUTSCENE_EVENT_PLAYER_STEP_REFUSED,
        "entity callback child incorrectly inherited cutscene ownership");
  CHECK(cutscene_event_player_step_owned_slot(&cpu, &window, 9u) ==
            CUTSCENE_EVENT_PLAYER_STEP_REFUSED,
        "window allowed an originally queued foreign callback");
  CHECK(cutscene_event_player_step_owned_slot(&cpu, &window, 6u) ==
            CUTSCENE_EVENT_PLAYER_STEP_REFUSED,
        "window allowed an insertion rejected by the owner predicate");
  cutscene_event_player_unwatch_insertions(&window);
}

static int pairs_equal(Pair left, Pair right) {
  return left.deadline_bits == right.deadline_bits && left.slot == right.slot;
}

static void check_remaining_pairs(const Pair *before, uint32_t before_count,
                                  Pair removed) {
  uint8_t matched[CUTSCENE_EVENT_PLAYER_CAPACITY] = {0};
  uint32_t after_count = RD32(OWNER + HEAP_COUNT);
  uint32_t index, other;
  CHECK(after_count + 1u == before_count,
        "exact step removed the wrong number of entries");
  for (index = 0; index < before_count; ++index) {
    int found = 0;
    if (pairs_equal(before[index], removed))
      continue;
    for (other = 0; other < after_count; ++other)
      if (!matched[other] && pairs_equal(before[index], read_pair(other))) {
        matched[other] = 1u;
        found = 1;
        break;
      }
    CHECK(found, "an unowned raw deadline/slot pair changed");
  }
}

static void test_arbitrary_heap_removal(void) {
  static const float deadlines[] = {1.0f,  10.0f, 2.0f, 11.0f,
                                    12.0f, 3.0f,  4.0f};
  static const uint32_t slots[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u};
  CutsceneEventOwnershipWindow window;
  Pair before[7], removed;
  uint32_t index;
  CPU cpu = fresh_cpu();

  reset_owner();
  capture_owner();
  CHECK(cutscene_event_player_window_begin(&window) == 1,
        "empty ownership window refused");
  insertion_is_owned = 1;
  CHECK(cutscene_event_player_watch_insertions(&window, owns_current_insertion,
                                               NULL) == 1,
        "could not watch arbitrary-removal insertions");
  for (index = 0; index < 7u; ++index)
    invoke_insert(slots[index], deadlines[index]);
  CHECK(cutscene_event_player_window_claim_new(&window) == 7,
        "window did not claim arbitrary-removal fixture");
  for (index = 0; index < 7u; ++index)
    before[index] = read_pair(index);
  removed = before[4];
  CHECK(cutscene_event_player_step_owned_slot(&cpu, &window, 4u) ==
            CUTSCENE_EVENT_PLAYER_STEP_RAN,
        "arbitrary owned slot did not execute");
  CHECK(read_pair(1u).deadline_bits == float_bits(4.0f),
        "last replacement did not repair upward");
  check_remaining_pairs(before, 7u, removed);

  {
    static const float down_deadlines[] = {1.0f, 2.0f, 3.0f, 4.0f,
                                           5.0f, 6.0f, 7.0f};
    reset_owner();
    cutscene_event_player_unwatch_insertions(&window);
    CHECK(cutscene_event_player_window_begin(&window) == 1,
          "downward-removal window refused");
    CHECK(cutscene_event_player_watch_insertions(
              &window, owns_current_insertion, NULL) == 1,
          "could not re-arm downward-removal insertion watch");
    for (index = 0; index < 7u; ++index)
      invoke_insert(slots[index], down_deadlines[index]);
    CHECK(cutscene_event_player_window_claim_new(&window) == 7,
          "window did not claim downward-removal fixture");
    for (index = 0; index < 7u; ++index)
      before[index] = read_pair(index);
    removed = before[1];
    CHECK(cutscene_event_player_step_owned_slot(&cpu, &window, 1u) ==
              CUTSCENE_EVENT_PLAYER_STEP_RAN,
          "downward arbitrary owned slot did not execute");
    CHECK(read_pair(1u).deadline_bits == float_bits(4.0f),
          "last replacement did not repair downward");
    check_remaining_pairs(before, 7u, removed);
  }
  cutscene_event_player_unwatch_insertions(&window);
}

static void test_corrupt_insertion_refusal(void) {
  CutsceneEventOwnershipWindow window;
  unsigned long faults = cutscene_event_player_insertion_faults();
  unsigned before_super;
  uint8_t before[HEAP_COUNT + 4u];

  reset_owner();
  CHECK(cutscene_event_player_window_begin(&window) == 1,
        "corrupt-insertion window refused initial valid state");
  CHECK(cutscene_event_player_watch_insertions(&window, owns_current_insertion,
                                               NULL) == 1,
        "could not watch corrupt insertion fixture");
  WR32(OWNER + HEAP_COUNT, CUTSCENE_EVENT_PLAYER_CAPACITY + 1u);
  memcpy(before, guest_memory_const_pointer(OWNER), sizeof before);
  before_super = super_calls;
  invoke_insert(20u, 1.0f);
  CHECK(super_calls == before_super,
        "corrupt pre-state reached the retail insertion body");
  CHECK(!memcmp(before, guest_memory_const_pointer(OWNER), sizeof before),
        "corrupt insertion pre-state was mutated");
  CHECK(!window.active &&
            cutscene_event_player_insertion_faults() == faults + 1u,
        "corrupt insertion pre-state did not invalidate ownership");

  reset_owner();
  CHECK(cutscene_event_player_watch_insertions(&window, owns_current_insertion,
                                               NULL) == 1,
        "could not re-arm corrupt insertion fixture");
  super_mode = 2;
  invoke_insert(20u, 1.0f);
  super_mode = 1;
  CHECK(!window.active &&
            cutscene_event_player_insertion_faults() == faults + 2u,
        "two-slot insertion was not rejected as corrupt");
  cutscene_event_player_unwatch_insertions(&window);
}

static void test_capacity_and_corruption_refusal(void) {
  CutsceneEventOwnershipWindow window;
  uint8_t before[HEAP_COUNT + 4u];
  uint32_t index;

  reset_owner();
  for (index = 0; index < CUTSCENE_EVENT_PLAYER_CAPACITY; ++index)
    CHECK(queue_event(index, (float)index),
          "evidenced callback capacity rejected a valid slot");
  CHECK(!queue_event(0u, 2000.0f),
        "callback queue accepted an event beyond capacity");
  capture_owner();
  CHECK(cutscene_event_player_window_begin(&window) == 1,
        "full evidenced-capacity queue failed validation");

  reset_owner();
  capture_owner();
  WR32(OWNER + HEAP_COUNT, CUTSCENE_EVENT_PLAYER_CAPACITY + 1u);
  memcpy(before, guest_memory_const_pointer(OWNER), sizeof before);
  CHECK(cutscene_event_player_window_begin(&window) == -1,
        "oversized heap was not refused");
  CHECK(!memcmp(before, guest_memory_const_pointer(OWNER), sizeof before),
        "oversized-heap refusal mutated guest state");
  {
    CPU cpu = fresh_cpu();
    cpu.ecx = OWNER;
    WR32(cpu.esp, 0xabcdef03u);
    WR32(cpu.esp + 4u, float_bits(1000.0f));
    calls[0] = '\0';
    call_count = 0;
    registered_fn(FN_EVENT_PUMP)(&cpu);
    CHECK(!memcmp(before, guest_memory_const_pointer(OWNER), sizeof before),
          "ordinary corrupt-state refusal mutated the queue");
    CHECK(call_count == 0u,
          "ordinary corrupt-state refusal executed a callback");
  }

  reset_owner();
  CHECK(queue_event(1u, 1.0f), "corruption fixture enqueue failed");
  capture_owner();
  WR32(bitmap_address(ALLOCATED_BITS, 1u), 0u);
  memcpy(before, guest_memory_const_pointer(OWNER), sizeof before);
  CHECK(cutscene_event_player_window_begin(&window) == -1,
        "allocated/live bitmap disagreement was not refused");
  CHECK(!memcmp(before, guest_memory_const_pointer(OWNER), sizeof before),
        "bitmap-corruption refusal mutated guest state");

  reset_owner();
  CHECK(queue_event(1u, 1.0f) && queue_event(2u, 2.0f),
        "heap-corruption fixture enqueue failed");
  capture_owner();
  WR32(entry_address(1u), float_bits(0.5f));
  CHECK(cutscene_event_player_window_begin(&window) == -1,
        "broken heap ordering was not refused");
}

int main(void) {
  if (guest_memory_init() != 0 ||
      guest_memory_map_fixed(ARENA_BASE, ARENA_SIZE, PROT_READ | PROT_WRITE) !=
          0) {
    fprintf(stderr, "FAIL: could not map isolated guest arena\n");
    return 1;
  }
  CHECK(registration_count == 2u && registered_fn(FN_EVENT_INSERT) &&
            registered_fn(FN_EVENT_PUMP),
        "event insertion/pump overrides did not self-register");
  test_early_insertion_binding();
  test_strict_ordinary_pump();
  test_window_claim_selection_and_followup();
  test_arbitrary_heap_removal();
  test_corrupt_insertion_refusal();
  test_capacity_and_corruption_refusal();
  return failures != 0u;
}

/*
 * The retail bodies these tests super-call into. Production reaches them
 * through x86_guest_body, so the test models the same seam rather than a
 * symbol per function -- and an entry point this test does not model is a
 * FAILURE that names itself, never a silent return.
 */
void x86_guest_body(CPU *C, const char *module, uint32_t linked_ep) {
  if (linked_ep == 0x004b2b40u && !strcmp(module, "XMen2.exe")) {
    guest_body_004b2b40(C);
    return;
  }
  fprintf(stderr,
          "%s: x86_guest_body(%s, 0x%08x) is not modelled by this test.\n",
          "test_cutscene_event_player.c", module, linked_ep);
  abort();
}
