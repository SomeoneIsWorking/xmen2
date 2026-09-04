/* BehavEd's timed context player, ported from XMen2.exe FUN_004d9640.
 *
 * The scheduler owns timing and context suspension. It does not own a
 * cutscene: the cutscene owner supplies the predicate used by
 * behaved_player_step_owned, which removes and resumes only one context that
 * belongs to that authored sequence. Ordinary game updates enter the same
 * implementation through the 004d9640 override and retain the retail strict
 * deadline < now rule.
 */
#include "behaved_player.h"

#include "behaved_context.h"
#include "x86rt.h"
#include "x86rt_native.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum {
  EXE_PREFERRED = 0x00400000u,
  FN_SCHEDULER_PUMP = 0x004d9640u,
  FN_SCHEDULER_SLOT_RELEASE = 0x004d5d00u,
  FN_CONTEXT_POOL_RELEASE = 0x004d7c10u,
  FN_MANAGER = 0x004d8770u,
  FN_CONTEXT_CLEANUP = 0x004d8ea0u,

  MANAGER_CONTEXT_POOL = 0x0002f2f4u,
  MANAGER_SCHEDULER = 0x0003a080u,
  CONTEXT_STRIDE = 0x000005c4u,
  MAX_CONTEXTS = 30u,

  SCHEDULER_CONTEXT_SLOTS = 0x000u,
  SCHEDULER_HEAP = 0x10cu,
  SCHEDULER_HEAP_COUNT = 0x200u,
  HEAP_ENTRY_BYTES = 8u
};

typedef struct HeapEntry {
  uint32_t deadline_bits;
  uint32_t slot;
} HeapEntry;

typedef struct ValidatedHeap {
  uint32_t manager;
  uint32_t scheduler;
  uint32_t count;
  HeapEntry entry[MAX_CONTEXTS];
  uint32_t context[MAX_CONTEXTS];
} ValidatedHeap;

static uint32_t exe_base(void) {
  const X86Module *module;
  for (module = x86_modules(); module; module = module->next)
    if (module->preferred == EXE_PREFERRED && module->base && *module->base)
      return *module->base;
  return 0;
}

static float entry_deadline(const HeapEntry *entry) {
  float deadline;
  memcpy(&deadline, &entry->deadline_bits, sizeof deadline);
  return deadline;
}

static uint32_t entry_address(uint32_t scheduler, uint32_t index) {
  return scheduler + SCHEDULER_HEAP + index * HEAP_ENTRY_BYTES;
}

static int read_entry(uint32_t scheduler, uint32_t index, HeapEntry *entry) {
  return x86_peek(entry_address(scheduler, index), entry, sizeof *entry);
}

static void write_entry(uint32_t scheduler, uint32_t index,
                        const HeapEntry *entry) {
  uint32_t address = entry_address(scheduler, index);
  WR32(address, entry->deadline_bits);
  WR32(address + 4u, entry->slot);
}

static void swap_entries(uint32_t scheduler, uint32_t left, uint32_t right) {
  HeapEntry a, b;
  read_entry(scheduler, left, &a);
  read_entry(scheduler, right, &b);
  write_entry(scheduler, left, &b);
  write_entry(scheduler, right, &a);
}

static int context_index(uint32_t manager, uint32_t context, uint32_t *index) {
  uint32_t pool = manager + MANAGER_CONTEXT_POOL;
  uint32_t distance;
  if (context < pool)
    return 0;
  distance = context - pool;
  if (distance % CONTEXT_STRIDE != 0u ||
      distance / CONTEXT_STRIDE >= MAX_CONTEXTS)
    return 0;
  *index = distance / CONTEXT_STRIDE;
  return 1;
}

static int validate_heap(uint32_t scheduler, ValidatedHeap *heap) {
  uint32_t count, index, used_slots = 0;
  if (scheduler < MANAGER_SCHEDULER ||
      !x86_peek32(scheduler + SCHEDULER_HEAP_COUNT, &count) ||
      count > MAX_CONTEXTS)
    return 0;

  memset(heap, 0, sizeof *heap);
  heap->manager = scheduler - MANAGER_SCHEDULER;
  heap->scheduler = scheduler;
  heap->count = count;
  for (index = 0; index < count; ++index) {
    HeapEntry *entry = &heap->entry[index];
    uint32_t context, ignored_context_index;
    if (!read_entry(scheduler, index, entry) || entry->slot >= MAX_CONTEXTS ||
        (used_slots & (1u << entry->slot)) != 0u ||
        isnan(entry_deadline(entry)) ||
        !x86_peek32(scheduler + SCHEDULER_CONTEXT_SLOTS + entry->slot * 4u,
                    &context) ||
        !context_index(heap->manager, context, &ignored_context_index))
      return 0;
    used_slots |= 1u << entry->slot;
    heap->context[index] = context;
  }
  return 1;
}

static void repair_heap(uint32_t scheduler, uint32_t count, uint32_t index) {
  uint32_t parent;
  HeapEntry here, other;

  while (index != 0u) {
    parent = (index - 1u) / 2u;
    read_entry(scheduler, index, &here);
    read_entry(scheduler, parent, &other);
    if (!(entry_deadline(&here) < entry_deadline(&other)))
      break;
    swap_entries(scheduler, index, parent);
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
      read_entry(scheduler, left, &here);
      read_entry(scheduler, right, &other);
      /* FUN_004d5d80 chooses the right child when deadlines tie. */
      if (!(entry_deadline(&here) < entry_deadline(&other)))
        child = right;
    }
    read_entry(scheduler, index, &here);
    read_entry(scheduler, child, &other);
    if (!(entry_deadline(&other) < entry_deadline(&here)))
      break;
    swap_entries(scheduler, index, child);
    index = child;
  }
}

static void remove_heap_entry(const ValidatedHeap *heap, uint32_t index) {
  uint32_t last = heap->count - 1u;
  WR32(heap->scheduler + SCHEDULER_HEAP_COUNT, last);
  if (index == last)
    return;
  write_entry(heap->scheduler, index, &heap->entry[last]);
  write_entry(heap->scheduler, last, &heap->entry[index]);
  repair_heap(heap->scheduler, last, index);
}

static uint32_t call_guest(const CPU *source, uint32_t target, uint32_t self,
                           int has_argument, uint32_t argument) {
  CPU call = *source;
  call.reg[kX86pEcx] = self;
  if (has_argument) {
    call.reg[kX86pEsp] -= 4u;
    WR32(call.reg[kX86pEsp], argument);
  }
  x86_guest_call_args(&call, target, has_argument ? 4u : 0u);
  return call.reg[kX86pEax];
}

static uint32_t manager(const CPU *cpu, uint32_t base) {
  return call_guest(cpu, base + (FN_MANAGER - EXE_PREFERRED), 0, 0, 0);
}

static BehavedPlayerStep execute_entry(CPU *cpu, uint32_t base,
                                       const ValidatedHeap *heap,
                                       uint32_t index) {
  uint32_t context = heap->context[index];
  uint32_t fiber_index;
  uint32_t completed;

  if (!context_index(heap->manager, context, &fiber_index))
    return BEHAVED_PLAYER_STEP_REFUSED;
  remove_heap_entry(heap, index);
  completed = behaved_context_run(cpu, context);
  if ((completed & 0xffu) == 1u) {
    uint32_t live_manager;
    call_guest(cpu, base + (FN_CONTEXT_CLEANUP - EXE_PREFERRED), context, 0, 0);
    live_manager = manager(cpu, base);
    if (live_manager == heap->manager)
      call_guest(cpu, base + (FN_CONTEXT_POOL_RELEASE - EXE_PREFERRED),
                 live_manager + MANAGER_CONTEXT_POOL, 1, fiber_index);
    else {
      call_guest(cpu, base + (FN_SCHEDULER_SLOT_RELEASE - EXE_PREFERRED),
                 heap->scheduler, 1, heap->entry[index].slot);
      return BEHAVED_PLAYER_STEP_REFUSED;
    }
  }
  call_guest(cpu, base + (FN_SCHEDULER_SLOT_RELEASE - EXE_PREFERRED),
             heap->scheduler, 1, heap->entry[index].slot);
  return (completed & 0xffu) == 1u ? BEHAVED_PLAYER_STEP_COMPLETED
                                   : BEHAVED_PLAYER_STEP_RAN;
}

static BehavedPlayerStep step_due(CPU *cpu, uint32_t base, uint32_t scheduler,
                                  float now) {
  ValidatedHeap heap;
  if (!validate_heap(scheduler, &heap))
    return BEHAVED_PLAYER_STEP_REFUSED;
  if (heap.count == 0u || !(entry_deadline(&heap.entry[0]) < now))
    return BEHAVED_PLAYER_STEP_NONE;
  return execute_entry(cpu, base, &heap, 0u);
}

int behaved_player_next_owned(CPU *cpu, BehavedPlayerOwnsContext owns,
                              void *opaque, uint32_t *context) {
  ValidatedHeap heap;
  uint32_t base, owner, index, selected = MAX_CONTEXTS;
  float earliest = 0.0f;

  if (!cpu || !owns || !context || !(base = exe_base()) ||
      !(owner = manager(cpu, base)) || owner > UINT32_MAX - MANAGER_SCHEDULER ||
      !validate_heap(owner + MANAGER_SCHEDULER, &heap) || heap.manager != owner)
    return -1;
  for (index = 0; index < heap.count; ++index) {
    float deadline;
    if (!owns(heap.context[index], opaque))
      continue;
    deadline = entry_deadline(&heap.entry[index]);
    if (selected == MAX_CONTEXTS || deadline < earliest) {
      selected = index;
      earliest = deadline;
    }
  }
  if (selected == MAX_CONTEXTS)
    return 0;
  *context = heap.context[selected];
  return 1;
}

BehavedPlayerStep behaved_player_step_context(CPU *cpu, uint32_t context) {
  ValidatedHeap heap;
  uint32_t base, owner, index;

  if (!cpu || !(base = exe_base()) || !(owner = manager(cpu, base)) ||
      owner > UINT32_MAX - MANAGER_SCHEDULER ||
      !validate_heap(owner + MANAGER_SCHEDULER, &heap) || heap.manager != owner)
    return BEHAVED_PLAYER_STEP_REFUSED;
  for (index = 0; index < heap.count; ++index)
    if (heap.context[index] == context)
      return execute_entry(cpu, base, &heap, index);
  return BEHAVED_PLAYER_STEP_NONE;
}

BehavedPlayerStep behaved_player_step_owned(CPU *cpu,
                                            BehavedPlayerOwnsContext owns,
                                            void *opaque) {
  uint32_t context;
  int found = behaved_player_next_owned(cpu, owns, opaque, &context);
  if (found < 0)
    return BEHAVED_PLAYER_STEP_REFUSED;
  if (found == 0)
    return BEHAVED_PLAYER_STEP_NONE;
  return behaved_player_step_context(cpu, context);
}

void x2_override_004d9640(CPU *cpu) {
  uint32_t base = exe_base();
  uint32_t now_bits = 0;
  float now;

  if (cpu && base && x86_peek32(cpu->reg[kX86pEsp] + 4u, &now_bits)) {
    BehavedPlayerStep result;
    memcpy(&now, &now_bits, sizeof now);
    do {
      result = step_due(cpu, base, cpu->reg[kX86pEcx], now);
    } while (result == BEHAVED_PLAYER_STEP_RAN ||
             result == BEHAVED_PLAYER_STEP_COMPLETED);
  }
  if (cpu)
    cpu->reg[kX86pEsp] += 8u; /* RET 4: return address plus float argument. */
}

__attribute__((constructor)) static void
x2_behaved_player_register_override(void) {
  /* The retail body remains callable through the JIT for differential A/B. */
  x86_register_override("XMen2.exe", FN_SCHEDULER_PUMP, x2_override_004d9640);
}
