/* threads_memory.c -- see threads_memory.h. */
#include "threads_memory.h"

#include "guest_heap.h"
#include "guest_teb.h"
#include "x2_log.h"

#define TIB_BYTES 0x1000u
#define STACK_DEFAULT (256u * 1024u)

int guest_thread_memory_alloc(GuestThread *t, uint32_t stack_bytes) {
  t->stack_bytes =
      stack_bytes ? ((stack_bytes + 0xFFFu) & ~0xFFFu) : STACK_DEFAULT;
  t->stack_base = guest_malloc(t->stack_bytes);
  t->tib = guest_malloc(TIB_BYTES);
  if (!t->stack_base || !t->tib) {
    x2_log_error("threads: no guest memory for a %u-byte stack and a "
                 "TIB; the thread is NOT created and the caller is told "
                 "so.\n",
                 t->stack_bytes);
    guest_thread_memory_free(t);
    return 0;
  }
  /* The thread's own copy of every image's __declspec(thread) data. */
  if (!guest_teb_tls_attach(t->tib)) {
    guest_thread_memory_free(t);
    return 0;
  }
  return 1;
}

void guest_thread_memory_free(GuestThread *t) {
  if (t->stack_base)
    guest_free(t->stack_base);
  if (t->tib) {
    guest_teb_tls_detach(t->tib);
    guest_free(t->tib);
  }
  t->stack_base = t->tib = 0;
}
