/*
 * gpu_pass_binds.c: a repeat of what the pass has bound is skipped, and
 * everything that makes a bind necessary -- another pipeline, another vertex
 * or index buffer, the same buffer after an upload, another element size,
 * another texture or sampler in any slot, another slot count, a new pass --
 * asks for it.
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
  int p1, p2, i1, i2, v1, v2;

  gpu_pass_binds_reset(&b);
  expect("the first pipeline", gpu_pass_binds_pipeline_changed(&b, &p1), 1);
  expect("the same pipeline", gpu_pass_binds_pipeline_changed(&b, &p1), 0);
  expect("another pipeline", gpu_pass_binds_pipeline_changed(&b, &p2), 1);
  expect("the first again", gpu_pass_binds_pipeline_changed(&b, &p1), 1);

  expect("the first vertex buffer", gpu_pass_binds_vertex_changed(&b, &v1, 3u),
         1);
  expect("the same vertex buffer", gpu_pass_binds_vertex_changed(&b, &v1, 3u),
         0);
  expect("the same vertex buffer after an upload",
         gpu_pass_binds_vertex_changed(&b, &v1, 4u), 1);
  expect("another vertex buffer", gpu_pass_binds_vertex_changed(&b, &v2, 4u),
         1);

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

  int t1, t2, s1, s2;
  const void *const set[4] = {&t1, &s1, &t2, &s2};
  const void *const set_texture[4] = {&t1, &s1, &t1, &s2};
  const void *const set_sampler[4] = {&t1, &s1, &t2, &s1};
  const void *const too_many[2 * kGpuPassFragmentSamplers + 2] = {0};
  expect("the first samplers", gpu_pass_binds_samplers_changed(&b, set, 2), 1);
  expect("the same samplers", gpu_pass_binds_samplers_changed(&b, set, 2), 0);
  expect("another texture in the last slot",
         gpu_pass_binds_samplers_changed(&b, set_texture, 2), 1);
  expect("another sampler in the last slot",
         gpu_pass_binds_samplers_changed(&b, set_sampler, 2), 1);
  expect("fewer slots", gpu_pass_binds_samplers_changed(&b, set_sampler, 1), 1);
  expect("more slots than a pass records",
         gpu_pass_binds_samplers_changed(&b, too_many,
                                         kGpuPassFragmentSamplers + 1),
         1);
  expect("more slots again, still bound",
         gpu_pass_binds_samplers_changed(&b, too_many,
                                         kGpuPassFragmentSamplers + 1),
         1);
  expect("samplers after the long set",
         gpu_pass_binds_samplers_changed(&b, set, 2), 1);

  gpu_pass_binds_reset(&b);
  expect("a new pass's samplers", gpu_pass_binds_samplers_changed(&b, set, 2),
         1);
  expect("a new pass's pipeline", gpu_pass_binds_pipeline_changed(&b, &p1), 1);
  expect("a new pass's vertex buffer",
         gpu_pass_binds_vertex_changed(&b, &v2, 4u), 1);
  expect("a new pass's index buffer",
         gpu_pass_binds_index_changed(&b, &i2, 8u, 4u), 1);
  expect("kept pipeline binds counted", (int)b.pipelines_kept, 1);
  expect("kept vertex binds counted", (int)b.vertices_kept, 1);
  expect("kept index binds counted", (int)b.indices_kept, 1);
  expect("kept sampler binds counted", (int)b.samplers_kept, 1);

  if (failures) {
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
  }
  printf("gpu_pass_binds: ok\n");
  return 0;
}
