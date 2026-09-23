/* gpu_pass_binds.c -- see gpu_pass_binds.h. */
#include "gpu_pass_binds.h"

static GpuPassBinds g_binds;

GpuPassBinds *gpu_pass_binds(void) { return &g_binds; }

void gpu_pass_binds_reset(GpuPassBinds *binds) {
  binds->pipeline = 0;
  binds->index_buffer = 0;
  binds->index_serial = 0;
  binds->index_size = 0;
}

int gpu_pass_binds_pipeline_changed(GpuPassBinds *binds, const void *pipeline) {
  if (binds->pipeline && binds->pipeline == pipeline) {
    binds->pipelines_kept++;
    return 0;
  }
  binds->pipeline = pipeline;
  return 1;
}

int gpu_pass_binds_index_changed(GpuPassBinds *binds, const void *buffer,
                                 uint64_t serial, unsigned element_size) {
  if (binds->index_buffer && binds->index_buffer == buffer &&
      binds->index_serial == serial && binds->index_size == element_size) {
    binds->indices_kept++;
    return 0;
  }
  binds->index_buffer = buffer;
  binds->index_serial = serial;
  binds->index_size = element_size;
  return 1;
}
