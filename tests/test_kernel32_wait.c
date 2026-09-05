#include "guest_memory.h"
#include "kernel32_handles.h"
#include "threads.h"
#include "winmm.h"
#include "x86rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void imp_KERNEL32_WaitForSingleObject(CPU *cpu);
void imp_KERNEL32_WaitForMultipleObjects(CPU *cpu);
static Handle handles[2];
static unsigned char memory[4096];
static double elapsed, signal_at;
static unsigned wakes, checks, failures;
static uint32_t last_error;

Handle *k32_handle_get(uint32_t handle, int kind) {
  if (handle < 1 || handle > 2 || (kind && handles[handle - 1].kind != kind))
    abort();
  return &handles[handle - 1];
}
void k32_set_last_error(uint32_t error) { last_error = error; }
uint32_t guest_current_tid(void) { return 999; }
double guest_clock_now_s(void) { return elapsed; }
uint32_t winmm_next_due_ms(uint32_t cap) { return cap > 1 ? 1 : cap; }
void guest_cond_wait_ms(unsigned ms) {
  elapsed += (double)ms / 1000.0;
  if (++wakes > 2000)
    abort();
}
void winmm_timers_pump(void) {
  if (signal_at > 0 && elapsed >= signal_at)
    handles[0].count = handles[1].count = 1;
}
void guest_thread_state_report(void) { abort(); }
void x86_diag_dump(void) { abort(); }

static void expect(int value, const char *what) {
  checks++;
  if (!value) {
    fprintf(stderr, "FAIL: %s\n", what);
    failures++;
  }
}
static CPU arguments(unsigned many, uint32_t milliseconds) {
  CPU c = {0};
  c.reg[kX86pEsp] = 0x100;
  WR32(0x104, many ? 2 : 1);
  WR32(0x108, many ? 0x200 : milliseconds);
  WR32(0x10C, many == 2);
  WR32(0x110, milliseconds);
  WR32(0x200, 1);
  WR32(0x204, 2);
  return c;
}
int main(void) {
  g_guest_memory_base = (uintptr_t)memory;
  for (unsigned many = 0; many < 3; many++) {
    memset(handles, 0, sizeof handles);
    handles[0].kind = handles[1].kind = H_THREAD;
    for (unsigned pass = 0; pass < 3; pass++) {
      CPU c = arguments(many, pass == 2 ? 0 : 100);
      elapsed = 0;
      wakes = 0;
      signal_at = pass == 1 ? 0.050 : 0;
      if (many)
        imp_KERNEL32_WaitForMultipleObjects(&c);
      else
        imp_KERNEL32_WaitForSingleObject(&c);
      expect(c.reg[kX86pEax] == (pass ? 0u : 258u), "thread wait result");
      expect(c.reg[kX86pEsp] == (many ? 0x114u : 0x10Cu), "stdcall stack");
      expect(handles[0].waiters == 0 && handles[1].waiters == 0,
             "waiter release");
      if (pass == 0)
        expect(elapsed >= 0.100 && wakes >= 100,
               "early wake does not expire deadline");
      if (pass == 1)
        expect(elapsed >= 0.050 && elapsed < 0.100,
               "thread completion wins before timeout");
      if (pass == 2)
        expect(wakes == 0 && handles[0].count == 1,
               "completion stays signaled");
    }
  }
  expect(last_error == 0, "valid waits preserve last error");
  printf("%u wait checks, %u failures\n", checks, failures);
  return failures ? 1 : 0;
}
