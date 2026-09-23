/*
 * gpu_pass_binds.c: a repeat of what the pass has bound is skipped, and
 * everything that makes a bind necessary -- another pipeline, another index
 * buffer, the same buffer after an upload, another element size, a new
 * pass -- asks for it.
 */
#include "gpu_pass_binds.h"

#include <stdio.h>

static int failures;

static void expect(const char *what, int got, int want) {
  if (got != want) {
    fprintf(stderr, "FAIL %s: got %d, want %d\n", what, got, want);
    failures++;
  }
}

int main(void) {
  GpuPassBinds b = {0};
  int p1, p2, i1, i2;

  gpu_pass_binds_reset(&b);
  expect("the first pipeline", gpu_pass_binds_pipeline_changed(&b, &p1), 1);
  expect("the same pipeline", gpu_pass_binds_pipeline_changed(&b, &p1), 0);
  expect("another pipeline", gpu_pass_binds_pipeline_changed(&b, &p2), 1);
  expect("the first again", gpu_pass_binds_pipeline_changed(&b, &p1), 1);

  expect("the first index buffer",
         gpu_pass_binds_index_changed(&b, &i1, 7u, 2u), 1);
  expect("the same index buffer", gpu_pass_binds_index_changed(&b, &i1, 7u, 2u),
         0);
  expect("the same buffer after an upload",
         gpu_pass_binds_index_changed(&b, &i1, 8u, 2u), 1);
  expect("the same buffer at another element size",
         gpu_pass_binds_index_changed(&b, &i1, 8u, 4u), 1);
  expect("another index buffer", gpu_pass_binds_index_changed(&b, &i2, 8u, 4u),
         1);

  gpu_pass_binds_reset(&b);
  expect("a new pass's pipeline", gpu_pass_binds_pipeline_changed(&b, &p1), 1);
  expect("a new pass's index buffer",
         gpu_pass_binds_index_changed(&b, &i2, 8u, 4u), 1);
  expect("kept pipeline binds counted", (int)b.pipelines_kept, 1);
  expect("kept index binds counted", (int)b.indices_kept, 1);

  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("gpu_pass_binds: ok\n");
  return 0;
}
