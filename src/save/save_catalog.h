#pragma once

#include <cstdint>

namespace x2::save {

inline constexpr int kSaveLeafCapacity = 16;
inline constexpr unsigned kAutosaveSlots = 5u;

struct SaveCandidate {
  char leaf[kSaveLeafCapacity];
  int64_t mtime_ns;
};

/* autosave0.save .. autosave4.save. */
void autosave_slot_leaf(unsigned slot, char out[kSaveLeafCapacity]);
int autosave_leaf_slot(const char *leaf, unsigned *slot);

/* Find the newest regular save leaf in directory. Only saveslot0..9.save and
   the autosave slots are candidates. Equal timestamps choose the
   lexicographically greater leaf so directory enumeration order cannot affect
   the result.

   Returns 1 when a candidate was found, 0 when there are no candidates or the
   directory does not exist yet, and -1 for an invalid argument or any other
   filesystem error. Save contents are opaque. */
int save_catalog_latest(const char *directory, SaveCandidate *out);

/* The present autosave slots, newest first. Returns their count, 0 when the
   directory does not exist yet, or -1 on error. */
int save_catalog_autosaves(const char *directory,
                           SaveCandidate out[kAutosaveSlots]);

/* The slot the next autosave replaces: the lowest empty slot, else the
   oldest. Returns 0 on error. */
int save_catalog_next_autosave(const char *directory,
                               char leaf[kSaveLeafCapacity]);

} // namespace x2::save
