#pragma once

#include <cstdint>

namespace x2::save {

inline constexpr int kSaveLeafCapacity = 16;

struct SaveCandidate {
  char leaf[kSaveLeafCapacity];
  int64_t mtime_ns;
};

/* Find the newest regular save leaf in directory. Only saveslot0..9.save and
   autosave.save are candidates. Equal timestamps choose the lexicographically
   greater leaf so directory enumeration order cannot affect the result.

   Returns 1 when a candidate was found, 0 when there are no candidates or the
   directory does not exist yet, and -1 for an invalid argument or any other
   filesystem error. Save contents are opaque. */
int save_catalog_latest(const char *directory, SaveCandidate *out);

} // namespace x2::save
