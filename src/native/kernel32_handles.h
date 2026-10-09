/* Private handle-table contract shared by KERNEL32 services and waits. */
#pragma once
#include "platform_dirent.h"
#include <cstddef>
#include <cstdint>

namespace x2::native {
inline constexpr int MAX_HANDLES = 256;
inline constexpr int H_FILE = 1;
inline constexpr int H_FIND = 2;
inline constexpr int H_MAP = 3;
inline constexpr int H_SEM = 4;
inline constexpr int H_EVENT = 5;
inline constexpr int H_MUTEX = 6;
inline constexpr int H_THREAD = 7;

struct Handle {
  int kind;
  int fd;
  DIR *dir;
  char pattern[256];
  char dirpath[1024];
  void *map;
  size_t maplen;
  /* How many guest threads are blocked on this object right now, and how
     many times it has been PULSED. Both exist for PulseEvent, which needs
     them exactly: a pulse with no waiter is LOST on Windows, and a pulse on
     a manual-reset event releases every thread waiting AT THAT INSTANT and
     no later one -- which is a generation number, not a flag. A waiter
     records the count it entered on and is released when it changes. */
  int waiters;
  unsigned long pulses;
  /*
   * The history of this object, for the watchdog.
   *
   * Issue #57 stalls in WaitForSingleObject(INFINITE) on an unnamed event,
   * and the report named its KIND and its (empty) name -- which is every
   * unnamed event in the process. The issue's own next-measurement is
   * "which event is it, who created it, has it ever been signalled". These
   * are that, recorded as they happen because the answer is needed at a
   * moment when nothing can be asked any more.
   */
  uint32_t created_by; /* guest return address of its creator */
  unsigned long n_set, n_pulse_sent, n_pulse_lost, n_wait;
  /* Synchronisation objects. */
  int32_t count; /* semaphore count, or event signalled, or mutex depth */
  /* WHICH guest thread holds this mutex. A Win32 mutex excludes across
     threads and is recursive only for its owner, so a depth alone cannot
     express it: the old code took an unheld mutex unconditionally with the
     comment "this thread is the only one", which stopped being true the day
     the game created 23. */
  uint32_t owner_tid;
  int32_t maxcount; /* semaphore ceiling */
  int manual;       /* event: manual-reset rather than auto-reset */
  /* All duplicated H_THREAD handles point at the SAME GuestThread object.
     The numeric handle is an alias, not the thread's identity. */
  void *thread_rec;
  char name[128];
};

Handle *k32_handle_get(uint32_t handle, int kind);

/* A free slot of the given kind, zeroed with fd -1; stops the process when the
   table is full. */
uint32_t k32_handle_alloc(int kind);

/* The handle-table half of a guest thread: threads.cpp owns the thread. */
uint32_t k32_handle_for_thread(void *rec);
void *k32_thread_record(uint32_t handle);
unsigned k32_thread_handle_count(void *rec);
void k32_handle_thread_done(void *rec);
int kernel32_thread_alias_selftest(void);

/* The blocking waits' own counters: how many, how long they asked
   the scheduler to sleep, how long they slept, and the worst the
   sleep overshot by. See the note in kernel32_wait.cpp. */
void kernel32_wait_counts(unsigned long *sleeps, unsigned long long *asked_ms,
                          unsigned long long *slept_ms,
                          unsigned long *worst_oversleep_ms);
void k32_set_last_error(uint32_t error);
/* PulseEvent totals: pulses sent, and pulses lost for want of a waiter. */
void kernel32_pulse_counts(unsigned long *sent, unsigned long *lost);

} // namespace x2::native
