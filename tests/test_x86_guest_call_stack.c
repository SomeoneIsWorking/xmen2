/*
 * The title's live guest-call stack is per-thread (issue #140): a
 * shared one had one guest thread reading another's return_to and running past
 * its own 0xDEADBEEF entry sentinel. These check the single-thread contract and
 * that two threads keep independent stacks.
 */
#include "x86_guest_call_stack.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>

static int checks;
#define CHECK(c)                                                               \
  do {                                                                         \
    assert(c);                                                                 \
    checks++;                                                                  \
  } while (0)

static void single_thread_contract(void) {
  X86GuestCallFrame first, second, third;
  struct X86pCpu *first_cpu = (struct X86pCpu *)(uintptr_t)0x100u;
  struct X86pCpu *other_cpu = (struct X86pCpu *)(uintptr_t)0x200u;
  CHECK(x86_guest_call_depth() == 0);
  CHECK(x86_guest_call_top() == NULL);
  CHECK(x86_guest_call_for_cpu(first_cpu) == NULL); /* no-frame rejection */

  x86_guest_call_push(&first, first_cpu, 0x1000u, 0xDEADBEEFu, 0x200000u);
  CHECK(x86_guest_call_for_cpu(other_cpu) == NULL); /* CPU mismatch rejection */
  CHECK(x86_guest_call_for_cpu(first_cpu) == &first);
  x86_guest_call_push(&second, first_cpu, 0x2000u, 0x1005u, 0x1ff000u);
  CHECK(x86_guest_call_depth() == 2);
  CHECK(x86_guest_call_top()->entry == 0x2000u);
  CHECK(x86_guest_call_top()->return_to == 0x1005u);
  CHECK(x86_guest_call_top()->previous == &first);

  /* A longjmp unwinds the nested host frame; restore takes the top back to the
     surviving frame without retaining a pointer to dead stack storage. */
  x86_guest_call_push(&third, NULL, 0x3000u, 0x2005u, 0x1fe000u);
  CHECK(x86_guest_call_depth() == 3);
  x86_guest_call_restore(&second);
  CHECK(x86_guest_call_depth() == 2);
  CHECK(x86_guest_call_top()->entry == 0x2000u);

  x86_guest_call_pop(&second);
  x86_guest_call_pop(&first);
  CHECK(x86_guest_call_depth() == 0);
  CHECK(x86_guest_call_top() == NULL);

  /* Depth has no shadow-array limit: every live host frame supplies its node.
   */
  X86GuestCallFrame deep[96];
  for (int i = 0; i < 96; i++)
    x86_guest_call_push(&deep[i], NULL, (uint32_t)(0x10000u + i), 0u, 0u);
  CHECK(x86_guest_call_depth() == 96u);
  CHECK(x86_guest_call_top() == &deep[95]);
  for (int i = 95; i >= 0; i--)
    x86_guest_call_pop(&deep[i]);
  CHECK(x86_guest_call_depth() == 0);
}

static pthread_barrier_t barrier;

static void *other_thread(void *arg) {
  X86GuestCallFrame first, second;
  (void)arg;
  CHECK(x86_guest_call_depth() == 0); /* not the main thread's frames */
  x86_guest_call_push(&first, NULL, 0xBBBB0000u, 0xBBBB1111u, 0xB000u);
  x86_guest_call_push(&second, NULL, 0xBBBB2222u, 0xBBBB3333u, 0xB100u);
  pthread_barrier_wait(&barrier); /* main pushes its own frames here */
  pthread_barrier_wait(
      &barrier); /* main has checked; verify we are untouched */
  CHECK(x86_guest_call_depth() == 2);
  CHECK(x86_guest_call_top()->entry == 0xBBBB2222u);
  CHECK(x86_guest_call_top()->return_to == 0xBBBB3333u);
  x86_guest_call_pop(&second);
  x86_guest_call_pop(&first);
  return NULL;
}

static void two_threads_are_independent(void) {
  pthread_t t;
  X86GuestCallFrame frame;
  pthread_barrier_init(&barrier, NULL, 2);
  CHECK(pthread_create(&t, NULL, other_thread, NULL) == 0);

  pthread_barrier_wait(&barrier); /* other has pushed 2 */
  CHECK(x86_guest_call_depth() == 0);
  x86_guest_call_push(&frame, NULL, 0xAAAA0000u, 0xAAAA1111u, 0xA000u);
  CHECK(x86_guest_call_depth() == 1);
  CHECK(x86_guest_call_top()->entry == 0xAAAA0000u);
  pthread_barrier_wait(&barrier); /* let other verify and unwind */

  pthread_join(t, NULL);
  x86_guest_call_pop(&frame);
  CHECK(x86_guest_call_depth() == 0);
  pthread_barrier_destroy(&barrier);

  /* deepest is the cross-thread high-water. */
  CHECK(x86_guest_call_deepest() >= 2);
}

int main(void) {
  single_thread_contract();
  x86_guest_call_reset_deepest();
  two_threads_are_independent();
  printf("test_x86_guest_call_stack: %d check(s) passed\n", checks);
  return 0;
}
