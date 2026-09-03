#include "control_status.h"

#include "gpu_device.h"
#include "guest_clock.h"
#include "input_record.h"
#include "json_string.h"

#include <stdio.h>
#include <unistd.h>

size_t control_status_format(char *body, size_t capacity,
                             unsigned long requests, unsigned long keys_pressed,
                             unsigned long keys_refused,
                             unsigned long screenshots) {
  unsigned long long frame_ns = 0, minimum = 0, maximum = 0, submits = 0;
  unsigned long long p50 = 0, p95 = 0, p99 = 0;
  unsigned long intervals = 0;
  unsigned long samples = 0;
  const unsigned long *histogram = NULL;
  char recording_json[4096], backend_json[128];
  uint32_t presentation_width = 0, presentation_height = 0;
  int size;

  if (!body || !capacity ||
      !json_string_format(recording_json, sizeof recording_json,
                          input_record_path()) ||
      !json_string_format(backend_json, sizeof backend_json,
                          gpu_device_backend()))
    return 0;
  gpu_device_perf(&frame_ns, &minimum, &maximum, &submits, &intervals,
                  &histogram);
  gpu_device_frame_percentiles(&p50, &p95, &p99, &samples);
  gpu_device_presentation_size(&presentation_width, &presentation_height);
  size = snprintf(
      body, capacity,
      "{\n"
      "  \"frames_presented\": %lu,\n"
      "  \"guest_time_s\": %.3f,\n"
      "  \"unbounded\": %s,\n"
      "  \"renderer_ready\": %s,\n"
      "  \"renderer_backend\": %s,\n"
      "  \"presentation_width\": %u,\n"
      "  \"presentation_height\": %u,\n"
      "  \"frame_ms_avg\": %.3f,\n"
      "  \"frame_ms_min\": %.3f,\n"
      "  \"frame_ms_max\": %.3f,\n"
      "  \"frame_ms_p50\": %.3f,\n"
      "  \"frame_ms_p95\": %.3f,\n"
      "  \"frame_ms_p99\": %.3f,\n"
      "  \"frame_sample_count\": %lu,\n"
      "  \"frame_intervals\": %lu,\n"
      "  \"pid\": %ld,\n"
      "  \"input_recording\": { \"path\": %s, \"events\": %lu },\n"
      "  \"control\": { \"requests\": %lu, \"keys_pressed\": %lu,"
      " \"keys_refused\": %lu, \"screenshots\": %lu }\n"
      "}\n",
      gpu_frames_presented(), guest_clock_elapsed_s(),
      guest_clock_unbounded() ? "true" : "false",
      gpu_device_ready() ? "true" : "false", backend_json, presentation_width,
      presentation_height, intervals ? (double)frame_ns / intervals / 1e6 : 0.0,
      minimum / 1e6, maximum / 1e6, p50 / 1e6, p95 / 1e6, p99 / 1e6, samples,
      intervals, (long)getpid(), recording_json, input_record_event_count(),
      requests, keys_pressed, keys_refused, screenshots);
  return size > 0 && (size_t)size < capacity ? (size_t)size : 0;
}
