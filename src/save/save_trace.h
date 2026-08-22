#ifndef X2_SAVE_TRACE_H
#define X2_SAVE_TRACE_H

#include <stddef.h>
#include <stdint.h>

#define SAVE_TRACE_EVENT_CAPACITY 64u
#define SAVE_TRACE_LABEL_CAPACITY 48u

typedef enum {
    SAVE_TRACE_MENU_BUILD,
    SAVE_TRACE_MENU_OPEN,
    SAVE_TRACE_MAIN_ENGB_OPEN,
    SAVE_TRACE_LOAD_0055FCD0,
    SAVE_TRACE_LOAD_004AED10,
    SAVE_TRACE_LOAD_0046E2B0,
    SAVE_TRACE_LOAD_0049F140,
    SAVE_TRACE_SAVE_004AEB80,
    SAVE_TRACE_SAVE_004B15B0,
    SAVE_TRACE_SAVE_004B1746,
    SAVE_TRACE_SAVE_004B177A,
    SAVE_TRACE_SAVE_MODE_CYCLE,
    SAVE_TRACE_MAP_00484CE0,
    SAVE_TRACE_LOCK_COMBAT,
    SAVE_TRACE_EXTRACTION_SAVE_COMMAND,
    SAVE_TRACE_POINT_COUNT
} SaveTracePoint;

typedef enum {
    SAVE_TRACE_ANSWER_UNKNOWN,
    SAVE_TRACE_ANSWER_NO,
    SAVE_TRACE_ANSWER_YES
} SaveTraceAnswer;

typedef enum {
    SAVE_TRACE_RECORDED,
    SAVE_TRACE_REFUSED_DISABLED,
    SAVE_TRACE_REFUSED_INVALID,
    SAVE_TRACE_REFUSED_CAPACITY
} SaveTraceResult;

typedef struct {
    uint64_t attempts;
    uint64_t recorded;
    uint64_t yes;
    uint64_t no;
    uint64_t unknown;
} SaveTracePointStats;

typedef struct {
    uint64_t sequence;
    SaveTracePoint point;
    SaveTraceAnswer answer;
    uint32_t mode;
    uint32_t state;
    uint32_t device;
    uint32_t selection;
    uint32_t buffer;
    int label_truncated;
    char label[SAVE_TRACE_LABEL_CAPACITY];
} SaveTraceEvent;

typedef struct {
    int enabled;
    uint64_t attempts;
    uint64_t recorded;
    uint64_t refused_disabled;
    uint64_t refused_invalid;
    uint64_t overwritten;
    uint64_t truncated_labels;
    size_t first;
    size_t retained;
    SaveTracePointStats point[SAVE_TRACE_POINT_COUNT];
    SaveTraceEvent event[SAVE_TRACE_EVENT_CAPACITY];
} SaveTrace;

/* This collector is intentionally inert until enabled. Disabled observations
   retain no payload, but count as explicit refusals so an unwired capture and
   an observed zero cannot be mistaken for one another. It is guest-thread
   state and performs no allocation, I/O or logging. */
void save_trace_init(SaveTrace *trace, int enabled);
void save_trace_set_enabled(SaveTrace *trace, int enabled);

/* Record any named boundary. The five numeric fields are zero for a plain
   mark; `label` is copied into bounded storage and made single-line. */
SaveTraceResult save_trace_mark(SaveTrace *trace, SaveTracePoint point,
                                SaveTraceAnswer answer, const char *label);

/* Exact 004aed10 seam. `metadata_branch` says whether the selected metadata
   branch was taken; the remaining arguments are manager fields at entry. */
SaveTraceResult save_trace_load_manager(SaveTrace *trace,
                                        SaveTraceAnswer metadata_branch,
                                        uint32_t mode, uint32_t state,
                                        uint32_t device, uint32_t selection,
                                        uint32_t buffer);

/* Exact mode transition observed around the retail save path. */
SaveTraceResult save_trace_mode_cycle(SaveTrace *trace,
                                      uint32_t previous_mode,
                                      uint32_t next_mode);

const char *save_trace_point_name(SaveTracePoint point);
SaveTracePointStats save_trace_point_stats(const SaveTrace *trace,
                                           SaveTracePoint point);
size_t save_trace_event_count(const SaveTrace *trace);
int save_trace_event_at(const SaveTrace *trace, size_t chronological_index,
                        SaveTraceEvent *out);

/* Produce a complete, single-line-safe text report. Every point is present,
   including 0/0 points. The function refuses rather than truncating; required
   receives the byte count including the trailing NUL. */
SaveTraceResult save_trace_report(const SaveTrace *trace, char *out,
                                  size_t size, size_t *required);

#endif
