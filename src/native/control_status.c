#include "control_status.h"

#include "guest_clock.h"
#include "gpu_device.h"
#include "input_record.h"
#include "json_string.h"

#include <stdio.h>
#include <unistd.h>

size_t control_status_format(char *body, size_t capacity,
                             unsigned long requests,
                             unsigned long keys_pressed,
                             unsigned long keys_refused,
                             unsigned long screenshots)
{
    unsigned long long frame_ns = 0, minimum = 0, maximum = 0, submits = 0;
    unsigned long intervals = 0;
    const unsigned long *histogram = NULL;
    char recording_json[4096];
    int size;

    if (!body || !capacity ||
        !json_string_format(recording_json, sizeof recording_json,
                            input_record_path()))
        return 0;
    gpu_device_perf(&frame_ns, &minimum, &maximum, &submits, &intervals,
                    &histogram);
    size = snprintf(body, capacity,
        "{\n"
        "  \"frames_presented\": %lu,\n"
        "  \"guest_time_s\": %.3f,\n"
        "  \"unbounded\": %s,\n"
        "  \"renderer_ready\": %s,\n"
        "  \"frame_ms_avg\": %.3f,\n"
        "  \"frame_ms_min\": %.3f,\n"
        "  \"frame_ms_max\": %.3f,\n"
        "  \"frame_intervals\": %lu,\n"
        "  \"pid\": %ld,\n"
        "  \"input_recording\": { \"path\": %s, \"events\": %lu },\n"
        "  \"control\": { \"requests\": %lu, \"keys_pressed\": %lu,"
        " \"keys_refused\": %lu, \"screenshots\": %lu }\n"
        "}\n",
        gpu_frames_presented(), guest_clock_elapsed_s(),
        guest_clock_unbounded() ? "true" : "false",
        gpu_device_ready() ? "true" : "false",
        intervals ? (double)frame_ns / intervals / 1e6 : 0.0,
        minimum / 1e6, maximum / 1e6, intervals, (long)getpid(),
        recording_json, input_record_event_count(), requests, keys_pressed,
        keys_refused, screenshots);
    return size > 0 && (size_t)size < capacity ? (size_t)size : 0;
}
