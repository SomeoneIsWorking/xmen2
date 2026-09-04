/* Title ABI compatibility used by native overrides around x86-32 guest code.
 *
 * The native host translates the guest's 32-bit addresses through its guest
 * arena. Its x86 memory operands are explicitly unaligned: an x86 instruction
 * may load a qword at address+4, and expressing that as a C uint64_t pointer
 * is undefined and faults with SIGBUS on Apple Silicon. Architectural CPU
 * state is the embedded x86port context; this header only supplies the
 * title-facing memory and native-call ABI around it.
 */
#ifndef X86RT_H
#define X86RT_H

#include <setjmp.h>
#include <stdint.h>
#include <string.h>

#include "cpu.h"

#ifdef X86_NATIVE
extern uintptr_t g_guest_memory_base;
static inline void *x86_guest_pointer(uint32_t address) {
  return (void *)(g_guest_memory_base + (uintptr_t)address);
}
#else
static inline void *x86_guest_pointer(uint32_t address) {
  return (void *)(uintptr_t)address;
}
#endif

/* x86 permits unaligned integer and floating-point memory operands. memcpy is
   the C spelling that preserves that contract without promising alignment to
   an AArch64 compiler. These inline to ordinary unaligned loads/stores. */
static inline uint8_t x86_load8(uint32_t address) {
  uint8_t value;
  memcpy(&value, x86_guest_pointer(address), sizeof value);
  return value;
}
static inline uint16_t x86_load16(uint32_t address) {
  uint16_t value;
  memcpy(&value, x86_guest_pointer(address), sizeof value);
  return value;
}
static inline uint32_t x86_load32(uint32_t address) {
  uint32_t value;
  memcpy(&value, x86_guest_pointer(address), sizeof value);
  return value;
}
static inline uint64_t x86_load64(uint32_t address) {
  uint64_t value;
  memcpy(&value, x86_guest_pointer(address), sizeof value);
  return value;
}
static inline float x86_loadf32(uint32_t address) {
  float value;
  memcpy(&value, x86_guest_pointer(address), sizeof value);
  return value;
}
static inline double x86_loadf64(uint32_t address) {
  double value;
  memcpy(&value, x86_guest_pointer(address), sizeof value);
  return value;
}
extern volatile uint32_t x2_write_watch_addr;
extern void x2_write_watch_fire(uint32_t address, uint32_t value);
static inline void x86_store8_raw(uint32_t address, uint8_t value) {
  memcpy(x86_guest_pointer(address), &value, sizeof value);
}
static inline void x86_store16_raw(uint32_t address, uint16_t value) {
  memcpy(x86_guest_pointer(address), &value, sizeof value);
}
static inline void x86_store32_raw(uint32_t address, uint32_t value) {
  memcpy(x86_guest_pointer(address), &value, sizeof value);
}
static inline void x86_store64_raw(uint32_t address, uint64_t value) {
  if (x2_write_watch_addr == address)
    x2_write_watch_fire(address, (uint32_t)value);
  else if (x2_write_watch_addr == address + 4u)
    x2_write_watch_fire(address + 4u, (uint32_t)(value >> 32u));
  memcpy(x86_guest_pointer(address), &value, sizeof value);
}
static inline void x86_storef32(uint32_t address, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof bits);
  if (x2_write_watch_addr == address)
    x2_write_watch_fire(address, bits);
  memcpy(x86_guest_pointer(address), &value, sizeof value);
}
static inline void x86_storef64(uint32_t address, double value) {
  uint64_t bits;
  memcpy(&bits, &value, sizeof bits);
  if (x2_write_watch_addr == address)
    x2_write_watch_fire(address, (uint32_t)bits);
  else if (x2_write_watch_addr == address + 4u)
    x2_write_watch_fire(address + 4u, (uint32_t)(bits >> 32u));
  memcpy(x86_guest_pointer(address), &value, sizeof value);
}
/*
 * Native imports and overrides use the x86port CPU type directly. `CPU` is
 * only the title ABI's concise spelling; it does not wrap, copy, or mirror
 * architectural state at a guest/host hand-back.
 */
typedef X86pCpu CPU;

/* Runtime base of the current guest module for title diagnostics and import
   behavior. Guest images linked at the same preferred address are relocated
   to distinct mapped bases by the loader. */
extern uint32_t g_imgbase;
#ifndef X86_IMGBASE
#define X86_IMGBASE g_imgbase
#endif
extern uint32_t g_image_lo, g_image_hi; /* guest image bounds; outside = host */
/* Preferred base of the current guest image. Diagnostics use this to translate
   mapped addresses back to disassembly addresses. */
extern uint32_t g_guest_preferred_base;
/* An import with no native implementation. Names it and stops -- there is
   nothing honest to return. */
void x86_missing_import(const char *mod, const char *sym);
/* Call a guest function from HOST code: pushes the return address the body's
   RET will pop. Dispatching without it leaks guest stack, upward, silently. */
void x86_guest_call(CPU *C, uint32_t target);

/* ---- setjmp / longjmp --------------------------------------------------
 *
 * These are NOT an import stub, and cannot be: a host longjmp resumes into a
 * frame that must still be alive, and an import stub's frame is dead the
 * moment it returns. The JIT run therefore unwinds at the `_setjmp3` thunk and
 * takes the host setjmp in the still-live `x2_engine_call` frame.
 *
 * x86_setjmp_buf snapshots the guest register file against the guest's own
 * jmp_buf pointer (read from the stack, where _setjmp3's first argument sits)
 * and hands back the host buffer to jump into. x86_setjmp_done finishes the
 * call either way: rc == 0 is the direct return, anything else means a longjmp
 * arrived, so the snapshot is restored and rc becomes the return value.
 *
 * Both are __cdecl, so only the return address is popped.
 */
/* Whether `addr` is the import thunk for _setjmp3. The execution engine asks,
   because it must take the setjmp in its own live frame rather than let the
   stub record an unresumable one. */
int x86_setjmp3_thunk(uint32_t addr);
jmp_buf *x86_setjmp_buf(CPU *C);
void x86_setjmp_done(CPU *C, int rc);
/*
 * The buffer table gives slots back: a jmp_buf living in a guest heap block
 * that has been freed can no longer be jumped to. That is the ONLY rule -- see
 * crt.c for the second one the real run refuted. x86_setjmp_reclaim returns how
 * many it freed, so "reclaimed nothing" and "reclaimed everything" are
 * distinguishable; x86_setjmp_live is how many are held.
 */
int x86_setjmp_reclaim(void);
int x86_setjmp_live(void);
/* Dump the last crossings between guest and host, with ESP on both sides. */
void x86_ring_dump(void);

#define G_IMGBASE (X86_IMGBASE)

/* ---- guest memory ---------------------------------------------------- */
#define RDF32(a) ((long double)x86_loadf32((uint32_t)(a)))
#define RDF64(a) ((long double)x86_loadf64((uint32_t)(a)))
#define WRF32(a, v) x86_storef32((uint32_t)(a), (float)(v))
#define WRF64(a, v) x86_storef64((uint32_t)(a), (double)(v))
#define RD8(a) x86_load8((uint32_t)(a))
#define RD16(a) x86_load16((uint32_t)(a))
#define RD32(a) x86_load32((uint32_t)(a))
/* The guest write-watch. When x2_write_watch_addr is armed, WR8/16/32 report
   ANY write to it with a body backtrace -- the definitive catch for a stack
   overrun whose writer is a DIRECT call (invisible to the dispatch ring).
   Unarmed (default) it is one predictable compare. */
static inline void x86_store8(uint32_t address, uint8_t value) {
  if (x2_write_watch_addr && address == x2_write_watch_addr)
    x2_write_watch_fire(address, value);
  x86_store8_raw(address, value);
}
static inline void x86_store16(uint32_t address, uint16_t value) {
  if (x2_write_watch_addr && address == x2_write_watch_addr)
    x2_write_watch_fire(address, value);
  x86_store16_raw(address, value);
}
static inline void x86_store32(uint32_t address, uint32_t value) {
  if (x2_write_watch_addr && address == x2_write_watch_addr)
    x2_write_watch_fire(address, value);
  x86_store32_raw(address, value);
}
#define WR8(a, v) x86_store8((uint32_t)(a), (uint8_t)(v))
#define WR16(a, v) x86_store16((uint32_t)(a), (uint16_t)(v))
#define RD64(a) x86_load64((uint32_t)(a))
#define WR64(a, v) x86_store64_raw((uint32_t)(a), (uint64_t)(v))
/* WR32 is the hottest store in the guest; the watch is above, shared. */
#define WR32(a, v) x86_store32((uint32_t)(a), (uint32_t)(v))

/* Initialize the canonical x86port state, including architectural x87 and
   MXCSR defaults. */
static inline void cpu_reset(CPU *C) { x86p_cpu_reset(C); }

void x87_fault(const char *what);
long double x87_require_st0(const CPU *C, const char *what);
typedef void (*x86_override_fn)(CPU *C);

/* Native overrides that return through x87 use x86port's canonical stack. */
static inline void x87_push(CPU *C, long double value) {
  if (!x86p_x87_push(&C->x87, value))
    x87_fault("x87 stack overflow");
}

static inline long double x87_pop(CPU *C) {
  long double value = 0.0L;
  if (!x86p_x87_pop(&C->x87, &value))
    x87_fault("x87 stack underflow");
  return value;
}

/* ---- indirect dispatch ----
 * Virtual calls and function pointers resolve at runtime. An address with no
 * guest body is a hard error, never a silent no-op: continuing past an
 * unresolved call produces a plausible-looking run that is wrong.
 */
void x86_dispatch(CPU *C, uint32_t target);

#endif /* X86RT_H */
