#include "save_trace.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const POINT_NAME[SAVE_TRACE_POINT_COUNT] = {
    "menu_build",
    "menu_open",
    "main_engb_open",
    "load_0055fcd0",
    "load_004aed10",
    "load_0046e2b0",
    "load_0049f150",
    "save_004aeb80",
    "save_004b15b0",
    "save_004b1746",
    "save_004b177a",
    "save_mode_cycle",
    "map_00484ce0",
    "zone_request",
    "extraction_success_branch"
};

typedef struct {
    char *out;
    size_t size;
    size_t used;
} ReportWriter;

static int valid_point(SaveTracePoint point)
{
    return point >= SAVE_TRACE_MENU_BUILD && point < SAVE_TRACE_POINT_COUNT;
}

static int valid_answer(SaveTraceAnswer answer)
{
    return answer >= SAVE_TRACE_ANSWER_UNKNOWN
        && answer <= SAVE_TRACE_ANSWER_YES;
}

static const char *answer_name(SaveTraceAnswer answer)
{
    static const char *const NAME[] = {"unknown", "no", "yes"};
    return valid_answer(answer) ? NAME[answer] : "invalid";
}

static void write_report(ReportWriter *writer, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

static void write_report(ReportWriter *writer, const char *format, ...)
{
    va_list args;
    int count;

    va_start(args, format);
    count = vsnprintf(writer->out ? writer->out + writer->used : NULL,
                      writer->out ? writer->size - writer->used : 0,
                      format, args);
    va_end(args);
    if (count > 0) writer->used += (size_t)count;
}

static void copy_label(SaveTrace *trace, SaveTraceEvent *event,
                       const char *label)
{
    size_t i;

    if (!label) label = "";
    for (i = 0; label[i] && i + 1u < sizeof event->label; i++) {
        unsigned char c = (unsigned char)label[i];
        event->label[i] = c == '\n' || c == '\r' || c == '\t'
                              ? ' '
                              : c < 0x20u || c == 0x7fu ? '?' : (char)c;
    }
    event->label[i] = 0;
    if (label[i]) {
        event->label_truncated = 1;
        trace->truncated_labels++;
    }
}

static SaveTraceResult record(SaveTrace *trace, SaveTracePoint point,
                              SaveTraceAnswer answer, const char *label,
                              uint32_t mode, uint32_t state, uint32_t device,
                              uint32_t selection, uint32_t buffer)
{
    SaveTracePointStats *stats;
    SaveTraceEvent *event;
    size_t slot;

    if (!trace) return SAVE_TRACE_REFUSED_INVALID;
    trace->attempts++;
    if (!valid_point(point) || !valid_answer(answer)) {
        trace->refused_invalid++;
        return SAVE_TRACE_REFUSED_INVALID;
    }
    stats = &trace->point[point];
    stats->attempts++;
    if (!trace->enabled) {
        trace->refused_disabled++;
        return SAVE_TRACE_REFUSED_DISABLED;
    }

    if (trace->retained < SAVE_TRACE_EVENT_CAPACITY) {
        slot = (trace->first + trace->retained) % SAVE_TRACE_EVENT_CAPACITY;
        trace->retained++;
    } else {
        slot = trace->first;
        trace->first = (trace->first + 1u) % SAVE_TRACE_EVENT_CAPACITY;
        trace->overwritten++;
    }
    event = &trace->event[slot];
    memset(event, 0, sizeof *event);
    event->sequence = trace->recorded + 1u;
    event->point = point;
    event->answer = answer;
    event->mode = mode;
    event->state = state;
    event->device = device;
    event->selection = selection;
    event->buffer = buffer;
    copy_label(trace, event, label);

    trace->recorded++;
    stats->recorded++;
    if (answer == SAVE_TRACE_ANSWER_YES)
        stats->yes++;
    else if (answer == SAVE_TRACE_ANSWER_NO)
        stats->no++;
    else
        stats->unknown++;
    return SAVE_TRACE_RECORDED;
}

void save_trace_init(SaveTrace *trace, int enabled)
{
    if (!trace) return;
    memset(trace, 0, sizeof *trace);
    trace->enabled = enabled != 0;
}

void save_trace_set_enabled(SaveTrace *trace, int enabled)
{
    if (trace) trace->enabled = enabled != 0;
}

SaveTraceResult save_trace_mark(SaveTrace *trace, SaveTracePoint point,
                                SaveTraceAnswer answer, const char *label)
{
    return record(trace, point, answer, label, 0, 0, 0, 0, 0);
}

SaveTraceResult save_trace_load_manager(SaveTrace *trace,
                                        SaveTraceAnswer metadata_branch,
                                        uint32_t mode, uint32_t state,
                                        uint32_t device, uint32_t selection,
                                        uint32_t buffer)
{
    return record(trace, SAVE_TRACE_LOAD_004AED10, metadata_branch, NULL,
                  mode, state, device, selection, buffer);
}

SaveTraceResult save_trace_mode_cycle(SaveTrace *trace,
                                      uint32_t previous_mode,
                                      uint32_t next_mode)
{
    return record(trace, SAVE_TRACE_SAVE_MODE_CYCLE,
                  SAVE_TRACE_ANSWER_UNKNOWN, NULL,
                  previous_mode, next_mode, 0, 0, 0);
}

const char *save_trace_point_name(SaveTracePoint point)
{
    return valid_point(point) ? POINT_NAME[point] : NULL;
}

SaveTracePointStats save_trace_point_stats(const SaveTrace *trace,
                                           SaveTracePoint point)
{
    SaveTracePointStats empty = {0};
    return trace && valid_point(point) ? trace->point[point] : empty;
}

size_t save_trace_event_count(const SaveTrace *trace)
{
    return trace ? trace->retained : 0;
}

int save_trace_event_at(const SaveTrace *trace, size_t chronological_index,
                        SaveTraceEvent *out)
{
    size_t slot;

    if (!trace || !out || chronological_index >= trace->retained) return 0;
    slot = (trace->first + chronological_index) % SAVE_TRACE_EVENT_CAPACITY;
    *out = trace->event[slot];
    return 1;
}

static void render_report(const SaveTrace *trace, ReportWriter *writer)
{
    size_t i;

    write_report(writer,
                 "save-trace enabled=%d attempts=%" PRIu64
                 " recorded=%" PRIu64 " retained=%zu/%u"
                 " overwritten=%" PRIu64 " refused-disabled=%" PRIu64
                 " refused-invalid=%" PRIu64 " truncated-labels=%" PRIu64
                 "\n",
                 trace->enabled, trace->attempts, trace->recorded,
                 trace->retained, SAVE_TRACE_EVENT_CAPACITY,
                 trace->overwritten, trace->refused_disabled,
                 trace->refused_invalid, trace->truncated_labels);
    for (i = 0; i < SAVE_TRACE_POINT_COUNT; i++) {
        const SaveTracePointStats *stats = &trace->point[i];
        write_report(writer,
                     "point %s observed=%" PRIu64 "/%" PRIu64
                     " refused=%" PRIu64 " yes=%" PRIu64
                     " no=%" PRIu64 " unknown=%" PRIu64 "\n",
                     POINT_NAME[i], stats->recorded, stats->attempts,
                     stats->attempts - stats->recorded, stats->yes,
                     stats->no, stats->unknown);
    }
    for (i = 0; i < trace->retained; i++) {
        SaveTraceEvent event;
        save_trace_event_at(trace, i, &event);
        write_report(writer,
                     "event seq=%" PRIu64 " point=%s answer=%s"
                     " mode=%" PRIu32 " state=%" PRIu32
                     " device=%" PRIu32 " selection=%" PRIu32
                     " buffer=0x%08" PRIx32 " label=%s%s\n",
                     event.sequence, POINT_NAME[event.point],
                     answer_name(event.answer), event.mode, event.state,
                     event.device, event.selection, event.buffer, event.label,
                     event.label_truncated ? " [truncated]" : "");
    }
}

SaveTraceResult save_trace_report(const SaveTrace *trace, char *out,
                                  size_t size, size_t *required)
{
    ReportWriter measure = {0};
    ReportWriter writer;

    if (!trace) {
        if (out && size) out[0] = 0;
        if (required) *required = 0;
        return SAVE_TRACE_REFUSED_INVALID;
    }
    render_report(trace, &measure);
    if (required) *required = measure.used + 1u;
    if (!out || size <= measure.used) {
        if (out && size) out[0] = 0;
        return SAVE_TRACE_REFUSED_CAPACITY;
    }
    writer.out = out;
    writer.size = size;
    writer.used = 0;
    render_report(trace, &writer);
    return SAVE_TRACE_RECORDED;
}
