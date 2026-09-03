/*
 * The engine's interpreted-call frame stack is per-thread (issue #140): a
 * shared one had one guest thread reading another's return_to and running past
 * its own 0xDEADBEEF entry sentinel. These check the single-thread contract and
 * that two threads keep independent stacks.
 */
#include "x86_engine_frames.h"

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
  CHECK(engine_frame_depth() == 0);
  CHECK(engine_frame_top() == NULL);
  CHECK(engine_frame_at(0) == NULL);

  engine_frame_push(0x1000u, 0xDEADBEEFu, 0x200000u, NULL);
  engine_frame_push(0x2000u, 0x1005u, 0x1ff000u, NULL);
  CHECK(engine_frame_depth() == 2);
  CHECK(engine_frame_top()->entry == 0x2000u);
  CHECK(engine_frame_top()->return_to == 0x1005u);
  CHECK(engine_frame_at(0)->entry == 0x1000u);
  CHECK(engine_frame_at(1)->entry == 0x2000u);
  CHECK(engine_frame_at(2) == NULL);

  /* A longjmp unwinds the host frames the depth counted; restore takes it
     back so the deepest-nesting figure is not a record of a dead stack. */
  unsigned long saved = engine_frame_depth();
  engine_frame_push(0x3000u, 0x2005u, 0x1fe000u, NULL);
  CHECK(engine_frame_depth() == 3);
  engine_frame_restore_depth(saved);
  CHECK(engine_frame_depth() == 2);
  CHECK(engine_frame_top()->entry == 0x2000u);

  engine_frame_pop();
  engine_frame_pop();
  CHECK(engine_frame_depth() == 0);
  CHECK(engine_frame_top() == NULL);

  /* Frames past ENGINE_FRAMES_MAX count but are not stored. */
  for (int i = 0; i < ENGINE_FRAMES_MAX + 3; i++)
    engine_frame_push((uint32_t)(0x10000u + i), 0u, 0u, NULL);
  CHECK(engine_frame_depth() == (unsigned long)ENGINE_FRAMES_MAX + 3);
  CHECK(engine_frame_top() == NULL);
  CHECK(engine_frame_at(ENGINE_FRAMES_MAX - 1) != NULL);
  CHECK(engine_frame_at(ENGINE_FRAMES_MAX) == NULL);
  for (int i = 0; i < ENGINE_FRAMES_MAX + 3; i++)
    engine_frame_pop();
  CHECK(engine_frame_depth() == 0);
}

static pthread_barrier_t barrier;

static void *other_thread(void *arg) {
  (void)arg;
  CHECK(engine_frame_depth() == 0); /* not the main thread's frames */
  engine_frame_push(0xBBBB0000u, 0xBBBB1111u, 0xB000u, NULL);
  engine_frame_push(0xBBBB2222u, 0xBBBB3333u, 0xB100u, NULL);
  pthread_barrier_wait(&barrier); /* main pushes its own frames here */
  pthread_barrier_wait(&barrier); /* main has checked; verify we are untouched */
  CHECK(engine_frame_depth() == 2);
  CHECK(engine_frame_top()->entry == 0xBBBB2222u);
  CHECK(engine_frame_top()->return_to == 0xBBBB3333u);
  engine_frame_pop();
  engine_frame_pop();
  return NULL;
}

static void two_threads_are_independent(void) {
  pthread_t t;
  pthread_barrier_init(&barrier, NULL, 2);
  CHECK(pthread_create(&t, NULL, other_thread, NULL) == 0);

  pthread_barrier_wait(&barrier); /* other has pushed 2 */
  CHECK(engine_frame_depth() == 0);
  engine_frame_push(0xAAAA0000u, 0xAAAA1111u, 0xA000u, NULL);
  CHECK(engine_frame_depth() == 1);
  CHECK(engine_frame_top()->entry == 0xAAAA0000u);
  pthread_barrier_wait(&barrier); /* let other verify and unwind */

  pthread_join(t, NULL);
  engine_frame_pop();
  CHECK(engine_frame_depth() == 0);
  pthread_barrier_destroy(&barrier);

  /* deepest is the cross-thread high-water: the other thread reached 2. */
  CHECK(engine_frame_deepest() >= 2);
}

int main(void) {
  single_thread_contract();
  engine_frame_reset_deepest();
  two_threads_are_independent();
  printf("test_engine_frames: %d check(s) passed\n", checks);
  return 0;
}
