#include "save_trace.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

static void disabled_is_an_explicit_refusal(void)
{
    SaveTrace trace;
    SaveTracePointStats stats;
    char report[4096];
    int point;

    save_trace_init(&trace, 0);
    for (point = 0; point < SAVE_TRACE_POINT_COUNT; point++)
        CHECK(save_trace_point_name((SaveTracePoint)point) != NULL);
    CHECK(save_trace_point_name(SAVE_TRACE_POINT_COUNT) == NULL);
    CHECK(save_trace_mark(&trace, SAVE_TRACE_MENU_BUILD,
                          SAVE_TRACE_ANSWER_YES, "main")
          == SAVE_TRACE_REFUSED_DISABLED);
    stats = save_trace_point_stats(&trace, SAVE_TRACE_MENU_BUILD);
    CHECK(stats.attempts == 1);
    CHECK(stats.recorded == 0);
    CHECK(save_trace_event_count(&trace) == 0);
    CHECK(save_trace_report(&trace, report, sizeof report, NULL)
          == SAVE_TRACE_RECORDED);
    CHECK(strstr(report, "enabled=0 attempts=1 recorded=0") != NULL);
    CHECK(strstr(report,
                 "point menu_build observed=0/1 refused=1") != NULL);
    CHECK(strstr(report,
                 "point main_engb_open observed=0/0 refused=0") != NULL);
}

static void records_both_answers_and_manager_fields(void)
{
    SaveTrace trace;
    SaveTracePointStats stats;
    SaveTraceEvent event;
    char report[8192];

    save_trace_init(&trace, 1);
    CHECK(save_trace_mark(&trace, SAVE_TRACE_MAP_00484CE0,
                          SAVE_TRACE_ANSWER_YES, "act0/tutorial")
          == SAVE_TRACE_RECORDED);
    CHECK(save_trace_mark(&trace, SAVE_TRACE_MAP_00484CE0,
                          SAVE_TRACE_ANSWER_NO, "no-save")
          == SAVE_TRACE_RECORDED);
    CHECK(save_trace_load_manager(&trace, SAVE_TRACE_ANSWER_YES,
                                  5, 37, 2, 7, 0x12345678)
          == SAVE_TRACE_RECORDED);
    CHECK(save_trace_mode_cycle(&trace, 4, 5) == SAVE_TRACE_RECORDED);

    stats = save_trace_point_stats(&trace, SAVE_TRACE_MAP_00484CE0);
    CHECK(stats.attempts == 2 && stats.recorded == 2);
    CHECK(stats.yes == 1 && stats.no == 1 && stats.unknown == 0);
    CHECK(save_trace_event_at(&trace, 2, &event));
    CHECK(event.point == SAVE_TRACE_LOAD_004AED10);
    CHECK(event.answer == SAVE_TRACE_ANSWER_YES);
    CHECK(event.mode == 5 && event.state == 37);
    CHECK(event.device == 2 && event.selection == 7);
    CHECK(event.buffer == 0x12345678);
    CHECK(save_trace_report(&trace, report, sizeof report, NULL)
          == SAVE_TRACE_RECORDED);
    CHECK(strstr(report,
                 "point map_00484ce0 observed=2/2 refused=0 yes=1 no=1")
          != NULL);
    CHECK(strstr(report,
                 "point load_004aed10 observed=1/1 refused=0 yes=1")
          != NULL);
    CHECK(strstr(report,
                 "mode=5 state=37 device=2 selection=7 buffer=0x12345678")
          != NULL);
}

static void ring_is_bounded_and_chronological(void)
{
    SaveTrace trace;
    SaveTraceEvent event;
    size_t i;

    save_trace_init(&trace, 1);
    for (i = 0; i < SAVE_TRACE_EVENT_CAPACITY + 3u; i++) {
        CHECK(save_trace_mark(&trace, SAVE_TRACE_ZONE_REQUEST,
                              SAVE_TRACE_ANSWER_UNKNOWN, "zone")
              == SAVE_TRACE_RECORDED);
    }
    CHECK(trace.recorded == SAVE_TRACE_EVENT_CAPACITY + 3u);
    CHECK(trace.overwritten == 3);
    CHECK(save_trace_event_count(&trace) == SAVE_TRACE_EVENT_CAPACITY);
    CHECK(save_trace_event_at(&trace, 0, &event));
    CHECK(event.sequence == 4);
    CHECK(save_trace_event_at(&trace, SAVE_TRACE_EVENT_CAPACITY - 1u, &event));
    CHECK(event.sequence == SAVE_TRACE_EVENT_CAPACITY + 3u);
    CHECK(!save_trace_event_at(&trace, SAVE_TRACE_EVENT_CAPACITY, &event));
}

static void invalid_and_capacity_refusals_are_visible(void)
{
    static const char long_label[] =
        "a-zone-name-longer-than-the-fixed-event-label-capacity-by-design";
    SaveTrace trace;
    SaveTraceEvent event;
    char too_small[8] = "partial";
    char report[8192];
    size_t required = 0;

    save_trace_init(&trace, 1);
    CHECK(save_trace_mark(&trace, SAVE_TRACE_POINT_COUNT,
                          SAVE_TRACE_ANSWER_UNKNOWN, NULL)
          == SAVE_TRACE_REFUSED_INVALID);
    CHECK(trace.attempts == 1 && trace.refused_invalid == 1);
    CHECK(save_trace_mark(&trace, SAVE_TRACE_EXTRACTION_SUCCESS_BRANCH,
                          SAVE_TRACE_ANSWER_NO, long_label)
          == SAVE_TRACE_RECORDED);
    CHECK(save_trace_event_at(&trace, 0, &event));
    CHECK(event.label_truncated);
    CHECK(event.label[sizeof event.label - 1u] == 0);
    CHECK(trace.truncated_labels == 1);

    CHECK(save_trace_report(&trace, too_small, sizeof too_small, &required)
          == SAVE_TRACE_REFUSED_CAPACITY);
    CHECK(too_small[0] == 0);
    CHECK(required > sizeof too_small);
    CHECK(save_trace_report(&trace, NULL, 0, &required)
          == SAVE_TRACE_REFUSED_CAPACITY);
    CHECK(save_trace_report(&trace, report, sizeof report, NULL)
          == SAVE_TRACE_RECORDED);
    CHECK(strstr(report, "refused-invalid=1 truncated-labels=1") != NULL);
    CHECK(strstr(report, "[truncated]") != NULL);
    CHECK(save_trace_report(NULL, report, sizeof report, &required)
          == SAVE_TRACE_REFUSED_INVALID);
    CHECK(required == 0 && report[0] == 0);
}

int main(void)
{
    disabled_is_an_explicit_refusal();
    records_both_answers_and_manager_fields();
    ring_is_bounded_and_chronological();
    invalid_and_capacity_refusals_are_visible();
    printf("save_trace: %d checks; disabled/refusal, both answers, manager "
           "fields, bounds and denominators proved\n", checks);
    return 0;
}
