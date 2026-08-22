#include "cutscene_skip_publication.h"

CutsceneSkipPublicationSummary cutscene_skip_publication_classify(
    const CutsceneSkipPublicationBank
        bank[CUTSCENE_SKIP_PUBLICATION_BANKS])
{
    CutsceneSkipPublicationSummary result = {0};
    unsigned i;

    if (!bank) return result;
    for (i = 0; i < CUTSCENE_SKIP_PUBLICATION_BANKS; i++) {
        if (!bank[i].readable) continue;
        result.readable++;
        if (bank[i].escape) result.escape++;
        if (bank[i].start) result.start++;
    }
    return result;
}
