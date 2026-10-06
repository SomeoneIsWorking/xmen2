#ifndef X2_SAVE_CATALOG_H
#define X2_SAVE_CATALOG_H

#include <stdint.h>

#define X2_SAVE_LEAF_CAPACITY 16

typedef struct {
  char leaf[X2_SAVE_LEAF_CAPACITY];
  int64_t mtime_ns;
} X2SaveCandidate;

/* Find the newest regular save leaf in directory. Only saveslot0..9.save and
   autosave.save are candidates. Equal timestamps choose the lexicographically
   greater leaf so directory enumeration order cannot affect the result.

   Returns 1 when a candidate was found, 0 when there are no candidates or the
   directory does not exist yet, and -1 for an invalid argument or any other
   filesystem error. Save contents are opaque. */
int x2_save_catalog_latest(const char *directory, X2SaveCandidate *out);

#endif /* X2_SAVE_CATALOG_H */
