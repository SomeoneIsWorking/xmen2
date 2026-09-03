/* Port of XMen2.exe FUN_004b2b40 insertion and FUN_004b2d70's strict
 * deadline < now callback pump. Exact owned stepping changes selection only.
 */
#include "cutscene_event_player.h"
#include "guest_body.h"
#include "x86rt.h"
#include "x86rt_native.h"
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
enum {
  EXE_PREFERRED = 0x00400000u,
  CALLBACK_STRIDE = 0x18u,
  ALLOCATED_BITS = 0x6978u,
  LIVE_BITS = 0x7bacu,
  LIVE_COUNT = 0x7c3cu,
  HEAP = 0x7c40u,
  HEAP_COUNT = 0x9f6cu,
  HEAP_ENTRY_BYTES = 8u,
  FN_CALLBACK_EXECUTE = 0x004199f0u,
  FN_EVENT_INSERT = 0x004b2b40u,
  FN_EVENT_PUMP = 0x004b2d70u,
  FN_SLOT_FREE = 0x004b2ea0u
};
typedef struct EventEntry {
  uint32_t deadline_bits;
  uint32_t slot;
} EventEntry;
typedef struct ValidatedEvents {
  uint32_t owner;
  uint32_t count;
  EventEntry entry[CUTSCENE_EVENT_PLAYER_CAPACITY];
  uint32_t active[CUTSCENE_EVENT_PLAYER_SLOT_WORDS];
} ValidatedEvents;
static _Atomic uint32_t captured_owner;
static _Atomic unsigned long insertion_faults;
static CutsceneEventOwnershipWindow *watched_window;
static CutsceneEventInsertionOwner watched_owner;
static void *watched_opaque;
static unsigned causal_depth;
static uint32_t causal_inflight_slot;
static unsigned owned_execution_depth;
static uint32_t exe_base(void) {
  const X86Module *module;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && module->base && *module->base)
      return *module->base;
  return 0;
}
static float deadline_value(const EventEntry *entry) {
  float deadline;
  memcpy(&deadline, &entry->deadline_bits, sizeof deadline);
  return deadline;
}
static int read_entry(uint32_t owner, uint32_t index, EventEntry *entry) {
  return x86_peek(owner + HEAP + index * HEAP_ENTRY_BYTES, entry,
                  sizeof *entry);
}
static void write_entry(uint32_t owner, uint32_t index,
                        const EventEntry *entry) {
  uint32_t address = owner + HEAP + index * HEAP_ENTRY_BYTES;
  WR32(address, entry->deadline_bits);
  WR32(address + 4u, entry->slot);
}
static uint32_t slot_word(uint32_t slot) { return slot / 32u; }
static uint32_t slot_bit(uint32_t slot) { return 1u << (slot % 32u); }
static int mask_has(const uint32_t *mask, uint32_t slot) {
  return (mask[slot_word(slot)] & slot_bit(slot)) != 0u;
}
static void mask_set(uint32_t *mask, uint32_t slot) {
  mask[slot_word(slot)] |= slot_bit(slot);
}
static unsigned popcount32(uint32_t value) {
  unsigned count = 0;
  while (value) {
    value &= value - 1u;
    ++count;
  }
  return count;
}
static int validate_allocator(uint32_t owner, ValidatedEvents *events,
                              uint32_t inflight_slot) {
  uint32_t live_count, word;
  uint32_t expected[CUTSCENE_EVENT_PLAYER_SLOT_WORDS];
  unsigned populated = 0;
  unsigned extra = inflight_slot < CUTSCENE_EVENT_PLAYER_CAPACITY;
  if (!x86_peek32(owner + LIVE_COUNT, &live_count) ||
      live_count != events->count + extra)
    return 0;
  memcpy(expected, events->active, sizeof expected);
  if (extra) {
    if (mask_has(expected, inflight_slot))
      return 0;
    mask_set(expected, inflight_slot);
  }
  for (word = 0; word < CUTSCENE_EVENT_PLAYER_SLOT_WORDS; ++word) {
    uint32_t allocated, live;
    if (!x86_peek32(owner + ALLOCATED_BITS + word * 4u, &allocated) ||
        !x86_peek32(owner + LIVE_BITS + word * 4u, &live) ||
        allocated != expected[word] || live != expected[word])
      return 0;
    populated += popcount32(live);
  }
  return populated == live_count;
}
static int validate_events_with_inflight(uint32_t owner, uint32_t inflight_slot,
                                         ValidatedEvents *events) {
  uint32_t count, index;
  if (!owner || owner > UINT32_MAX - (HEAP_COUNT + 4u) ||
      !x86_peek32(owner + HEAP_COUNT, &count) ||
      count > CUTSCENE_EVENT_PLAYER_CAPACITY)
    return 0;
  memset(events, 0, sizeof *events);
  events->owner = owner;
  events->count = count;
  for (index = 0; index < count; ++index) {
    EventEntry *entry = &events->entry[index];
    float deadline;
    uint32_t callback;
    uint8_t callback_bytes[CALLBACK_STRIDE];
    if (!read_entry(owner, index, entry) ||
        entry->slot >= CUTSCENE_EVENT_PLAYER_CAPACITY ||
        mask_has(events->active, entry->slot))
      return 0;
    deadline = deadline_value(entry);
    if (isnan(deadline))
      return 0;
    if (index != 0u &&
        deadline < deadline_value(&events->entry[(index - 1u) / 2u]))
      return 0;
    callback = owner + entry->slot * CALLBACK_STRIDE;
    if (!x86_peek(callback, callback_bytes, sizeof callback_bytes))
      return 0;
    mask_set(events->active, entry->slot);
  }
  return validate_allocator(owner, events, inflight_slot);
}
static int validate_events(uint32_t owner, ValidatedEvents *events) {
  return validate_events_with_inflight(owner, CUTSCENE_EVENT_PLAYER_CAPACITY,
                                       events);
}
static void swap_entries(uint32_t owner, uint32_t left, uint32_t right) {
  EventEntry a, b;
  (void)read_entry(owner, left, &a);
  (void)read_entry(owner, right, &b);
  write_entry(owner, left, &b);
  write_entry(owner, right, &a);
}
/* FUN_004b2e10/FUN_004b2e60 sift the replacement in both directions. The
 * child selector chooses the right child on equal deadlines. */
static void repair_heap(uint32_t owner, uint32_t count, uint32_t index) {
  EventEntry here, other;
  while (index != 0u) {
    uint32_t parent = (index - 1u) / 2u;
    (void)read_entry(owner, index, &here);
    (void)read_entry(owner, parent, &other);
    if (!(deadline_value(&here) < deadline_value(&other)))
      break;
    swap_entries(owner, index, parent);
    index = parent;
  }
  for (;;) {
    uint32_t left = index * 2u + 1u;
    uint32_t right = left + 1u;
    uint32_t child;
    if (left >= count)
      break;
    child = left;
    if (right < count) {
      (void)read_entry(owner, left, &here);
      (void)read_entry(owner, right, &other);
      if (!(deadline_value(&here) < deadline_value(&other)))
        child = right;
    }
    (void)read_entry(owner, index, &here);
    (void)read_entry(owner, child, &other);
    if (!(deadline_value(&other) < deadline_value(&here)))
      break;
    swap_entries(owner, index, child);
    index = child;
  }
}
static void remove_entry(const ValidatedEvents *events, uint32_t index) {
  uint32_t last = events->count - 1u;
  WR32(events->owner + HEAP_COUNT, last);
  if (index == last)
    return;
  write_entry(events->owner, index, &events->entry[last]);
  write_entry(events->owner, last, &events->entry[index]);
  repair_heap(events->owner, last, index);
}
static void call_guest(const CPU *source, uint32_t target, uint32_t self,
                       int has_argument, uint32_t argument) {
  CPU call = *source;
  call.ecx = self;
  if (has_argument) {
    call.esp -= 4u;
    WR32(call.esp, argument);
  }
  x86_guest_call_args(&call, target, has_argument ? 4u : 0u);
}
static void execute_entry(CPU *cpu, uint32_t base,
                          const ValidatedEvents *events, uint32_t index) {
  uint32_t slot = events->entry[index].slot;
  uint32_t prior_inflight = causal_inflight_slot;
  int owned = watched_window && watched_window->active &&
              watched_window->owner == events->owner &&
              mask_has(watched_window->owned, slot);
  remove_entry(events, index);
  causal_inflight_slot = slot;
  ++causal_depth;
  if (owned)
    ++owned_execution_depth;
  call_guest(cpu, base + (FN_CALLBACK_EXECUTE - EXE_PREFERRED),
             events->owner + slot * CALLBACK_STRIDE, 0, 0u);
  if (owned)
    --owned_execution_depth;
  --causal_depth;
  causal_inflight_slot = prior_inflight;
  call_guest(cpu, base + (FN_SLOT_FREE - EXE_PREFERRED), events->owner, 1,
             slot);
}
static CutsceneEventPlayerStep step_due(CPU *cpu, uint32_t base, uint32_t owner,
                                        float now) {
  ValidatedEvents events;
  if (!validate_events(owner, &events))
    return CUTSCENE_EVENT_PLAYER_STEP_REFUSED;
  if (events.count == 0u || !(deadline_value(&events.entry[0]) < now))
    return CUTSCENE_EVENT_PLAYER_STEP_NONE;
  execute_entry(cpu, base, &events, 0u);
  return CUTSCENE_EVENT_PLAYER_STEP_RAN;
}
uint32_t cutscene_event_player_captured_owner(void) {
  return atomic_load_explicit(&captured_owner, memory_order_relaxed);
}
int cutscene_event_player_window_begin(CutsceneEventOwnershipWindow *window) {
  ValidatedEvents events;
  uint32_t owner = cutscene_event_player_captured_owner();
  if (!window)
    return -1;
  if (!owner)
    return 0;
  if (!validate_events(owner, &events))
    return -1;
  memset(window, 0, sizeof *window);
  window->owner = owner;
  memcpy(window->excluded, events.active, sizeof window->excluded);
  window->active = 1u;
  return 1;
}
int cutscene_event_player_window_claim_new(
    CutsceneEventOwnershipWindow *window) {
  ValidatedEvents events;
  unsigned claimed = 0;
  uint32_t word;
  if (!window || !window->active || !window->owner ||
      window->owner != cutscene_event_player_captured_owner() ||
      !validate_events(window->owner, &events))
    return -1;
  for (word = 0; word < CUTSCENE_EVENT_PLAYER_SLOT_WORDS; ++word) {
    uint32_t active = events.active[word];
    uint32_t newly_owned;
    window->owned[word] &= active;
    window->reported[word] &= active;
    newly_owned = window->owned[word] & ~window->reported[word];
    claimed += popcount32(newly_owned);
    window->reported[word] |= newly_owned;
    window->excluded[word] = active & ~window->owned[word];
  }
  return (int)claimed;
}
int cutscene_event_player_watch_insertions(
    CutsceneEventOwnershipWindow *window,
    CutsceneEventInsertionOwner owns_current, void *opaque) {
  ValidatedEvents events;
  uint32_t owner;
  if (!window || !owns_current)
    return -1;
  owner = cutscene_event_player_captured_owner();
  if (window->active) {
    if (!window->owner || (owner && window->owner != owner) ||
        !validate_events(window->owner, &events))
      return -1;
  } else if (owner) {
    if (!validate_events(owner, &events))
      return -1;
    memset(window, 0, sizeof *window);
    window->owner = owner;
    memcpy(window->excluded, events.active, sizeof window->excluded);
    window->active = 1u;
  } else {
    memset(window, 0, sizeof *window);
  }
  watched_window = window;
  watched_owner = owns_current;
  watched_opaque = opaque;
  return 1;
}
void cutscene_event_player_unwatch_insertions(
    CutsceneEventOwnershipWindow *window) {
  if (watched_window != window)
    return;
  watched_window = NULL;
  watched_owner = NULL;
  watched_opaque = NULL;
}
unsigned long cutscene_event_player_insertion_faults(void) {
  return atomic_load_explicit(&insertion_faults, memory_order_relaxed);
}
int cutscene_event_player_executing_owned(void) {
  return owned_execution_depth != 0u;
}
int cutscene_event_player_next_owned(const CutsceneEventOwnershipWindow *window,
                                     uint32_t *slot) {
  ValidatedEvents events;
  uint32_t index, selected = CUTSCENE_EVENT_PLAYER_CAPACITY;
  float earliest = 0.0f;
  if (!window || !slot || !window->active || !window->owner ||
      window->owner != cutscene_event_player_captured_owner() ||
      !validate_events(window->owner, &events))
    return -1;
  for (index = 0; index < events.count; ++index) {
    float deadline;
    if (!mask_has(window->owned, events.entry[index].slot))
      continue;
    deadline = deadline_value(&events.entry[index]);
    if (selected == CUTSCENE_EVENT_PLAYER_CAPACITY || deadline < earliest) {
      selected = index;
      earliest = deadline;
    }
  }
  if (selected == CUTSCENE_EVENT_PLAYER_CAPACITY)
    return 0;
  *slot = events.entry[selected].slot;
  return 1;
}
CutsceneEventPlayerStep cutscene_event_player_step_owned_slot(
    CPU *cpu, CutsceneEventOwnershipWindow *window, uint32_t slot) {
  ValidatedEvents events;
  uint32_t base, index;
  if (!cpu || !window || !window->active || !window->owner ||
      !(base = exe_base()) || slot >= CUTSCENE_EVENT_PLAYER_CAPACITY ||
      !mask_has(window->owned, slot) ||
      window->owner != cutscene_event_player_captured_owner() ||
      !validate_events(window->owner, &events))
    return CUTSCENE_EVENT_PLAYER_STEP_REFUSED;
  for (index = 0; index < events.count; ++index)
    if (events.entry[index].slot == slot) {
      execute_entry(cpu, base, &events, index);
      if (cutscene_event_player_window_claim_new(window) < 0) {
        window->active = 0u;
        return CUTSCENE_EVENT_PLAYER_STEP_RAN_CORRUPT;
      }
      return CUTSCENE_EVENT_PLAYER_STEP_RAN;
    }
  return CUTSCENE_EVENT_PLAYER_STEP_NONE;
}
CutsceneEventPlayerStep
cutscene_event_player_step_owned(CPU *cpu,
                                 CutsceneEventOwnershipWindow *window) {
  uint32_t slot;
  int found = cutscene_event_player_next_owned(window, &slot);
  if (found < 0)
    return CUTSCENE_EVENT_PLAYER_STEP_REFUSED;
  if (found == 0)
    return CUTSCENE_EVENT_PLAYER_STEP_NONE;
  return cutscene_event_player_step_owned_slot(cpu, window, slot);
}
static void insertion_fault(CutsceneEventOwnershipWindow *window) {
  atomic_fetch_add_explicit(&insertion_faults, 1u, memory_order_relaxed);
  if (window)
    window->active = 0u;
}
static int insertion_preserved(const ValidatedEvents *before,
                               const ValidatedEvents *after,
                               uint32_t *new_slot) {
  uint32_t before_index, after_index;
  unsigned new_count = 0;
  if (after->count != before->count + 1u)
    return 0;
  for (before_index = 0; before_index < before->count; ++before_index) {
    int found = 0;
    for (after_index = 0; after_index < after->count; ++after_index)
      if (before->entry[before_index].deadline_bits ==
              after->entry[after_index].deadline_bits &&
          before->entry[before_index].slot == after->entry[after_index].slot) {
        found = 1;
        break;
      }
    if (!found)
      return 0;
  }
  for (after_index = 0; after_index < CUTSCENE_EVENT_PLAYER_SLOT_WORDS;
       ++after_index) {
    uint32_t added = after->active[after_index] & ~before->active[after_index];
    while (added) {
      uint32_t bit = 0;
      uint32_t bits = added;
      while ((bits & 1u) == 0u) {
        bits >>= 1u;
        ++bit;
      }
      *new_slot = after_index * 32u + bit;
      ++new_count;
      added &= added - 1u;
    }
    if ((before->active[after_index] & ~after->active[after_index]) != 0u)
      return 0;
  }
  return new_count == 1u && *new_slot < CUTSCENE_EVENT_PLAYER_CAPACITY;
}
void x2_override_004b2b40(CPU *cpu) {
  ValidatedEvents before, after;
  CutsceneEventOwnershipWindow *destination = NULL;
  uint32_t owner, new_slot = 0;
  int owned = 0;
  if (!cpu || !(causal_depth ? validate_events_with_inflight(
                                   cpu->ecx, causal_inflight_slot, &before)
                             : validate_events(cpu->ecx, &before))) {
    insertion_fault(watched_window);
    if (cpu)
      cpu->esp += 12u; /* Refused RET 8, without guest mutation. */
    return;
  }
  owner = cpu->ecx;
  atomic_store_explicit(&captured_owner, owner, memory_order_relaxed);
  if (watched_window && watched_owner) {
    destination = watched_window;
    if (!destination->active) {
      memset(destination, 0, sizeof *destination);
      destination->owner = owner;
      memcpy(destination->excluded, before.active,
             sizeof destination->excluded);
      destination->active = 1u;
    }
    if (destination->owner != owner) {
      insertion_fault(destination);
      destination = NULL;
    } else {
      owned = watched_owner(cpu, watched_opaque) != 0;
    }
  }
  x86_guest_body(cpu, "XMen2.exe", 0x004b2b40u);
  if (!(causal_depth
            ? validate_events_with_inflight(owner, causal_inflight_slot, &after)
            : validate_events(owner, &after)) ||
      !insertion_preserved(&before, &after, &new_slot)) {
    insertion_fault(destination ? destination : watched_window);
    return;
  }
  if (!destination || !destination->active || destination->owner != owner)
    return;
  if (owned) {
    mask_set(destination->owned, new_slot);
    destination->excluded[slot_word(new_slot)] &= ~slot_bit(new_slot);
  } else {
    mask_set(destination->excluded, new_slot);
  }
}
void x2_override_004b2d70(CPU *cpu) {
  uint32_t base, now_bits;
  float now;
  if (cpu && (base = exe_base()) && x86_peek32(cpu->esp + 4u, &now_bits)) {
    ValidatedEvents events;
    memcpy(&now, &now_bits, sizeof now);
    if (validate_events(cpu->ecx, &events)) {
      CutsceneEventPlayerStep result;
      atomic_store_explicit(&captured_owner, cpu->ecx, memory_order_relaxed);
      do {
        result = step_due(cpu, base, cpu->ecx, now);
      } while (result == CUTSCENE_EVENT_PLAYER_STEP_RAN);
    }
  }
  if (cpu)
    cpu->esp += 8u; /* RET 4: return address plus float argument. */
}
__attribute__((constructor)) static void
x2_cutscene_event_player_register_override(void) {
  x86_register_override("XMen2.exe", FN_EVENT_INSERT, x2_override_004b2b40);
  x86_register_override("XMen2.exe", FN_EVENT_PUMP, x2_override_004b2d70);
}
