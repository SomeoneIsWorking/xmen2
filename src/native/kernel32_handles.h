/* Private handle-table contract shared by KERNEL32 services and waits. */
#ifndef X2_KERNEL32_HANDLES_H
#define X2_KERNEL32_HANDLES_H
#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#define MAX_HANDLES 256
#define H_FILE 1
#define H_FIND 2
#define H_MAP 3
#define H_SEM 4
#define H_EVENT 5
#define H_MUTEX 6
#define H_THREAD 7

typedef struct {
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
} Handle;

Handle *k32_handle_get(uint32_t handle, int kind);
void k32_set_last_error(uint32_t error);
#endif
