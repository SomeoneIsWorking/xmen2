#include "cutscene_skip_publication.h"

#include <assert.h>
#include <stdio.h>

static int checks;
#define CHECK(c) do { assert(c); checks++; } while (0)

static void check_summary(const CutsceneSkipPublicationBank bank[3],
                          unsigned readable, unsigned escape, unsigned start)
{
    CutsceneSkipPublicationSummary summary =
        cutscene_skip_publication_classify(bank);
    CHECK(summary.readable == readable);
    CHECK(summary.escape == escape);
    CHECK(summary.start == start);
}

int main(void)
{
    CutsceneSkipPublicationBank bank[3] = {
        {1, 1, 1}, {1, 1, 1}, {1, 1, 1}
    };
    unsigned i;

    check_summary(bank, 3, 3, 3);

    /* Removing Escape from every bank must not be hidden by Start. */
    for (i = 0; i < 3; i++) bank[i].escape = 0;
    check_summary(bank, 3, 0, 3);

    /* Removing Start from every bank must not be hidden by Escape. */
    for (i = 0; i < 3; i++) {
        bank[i].escape = 1;
        bank[i].start = 0;
    }
    check_summary(bank, 3, 3, 0);

    bank[1].readable = 0;
    check_summary(bank, 2, 2, 0);

    printf("test_cutscene_skip_publication: %d checks passed\n", checks);
    return 0;
}
