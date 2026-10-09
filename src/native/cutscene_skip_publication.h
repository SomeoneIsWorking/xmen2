#pragma once

namespace x2::native {

inline constexpr unsigned CUTSCENE_SKIP_PUBLICATION_BANKS = 3u;

struct CutsceneSkipPublicationBank {
  int readable;
  int escape;
  int start;
};

struct CutsceneSkipPublicationSummary {
  unsigned readable;
  unsigned escape;
  unsigned start;
};

CutsceneSkipPublicationSummary cutscene_skip_publication_classify(
    const CutsceneSkipPublicationBank bank[CUTSCENE_SKIP_PUBLICATION_BANKS]);

} // namespace x2::native
