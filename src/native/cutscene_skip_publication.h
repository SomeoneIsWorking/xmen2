#ifndef X2_CUTSCENE_SKIP_PUBLICATION_H
#define X2_CUTSCENE_SKIP_PUBLICATION_H

#define CUTSCENE_SKIP_PUBLICATION_BANKS 3u

typedef struct {
    int readable;
    int escape;
    int start;
} CutsceneSkipPublicationBank;

typedef struct {
    unsigned readable;
    unsigned escape;
    unsigned start;
} CutsceneSkipPublicationSummary;

CutsceneSkipPublicationSummary cutscene_skip_publication_classify(
    const CutsceneSkipPublicationBank
        bank[CUTSCENE_SKIP_PUBLICATION_BANKS]);

#endif
