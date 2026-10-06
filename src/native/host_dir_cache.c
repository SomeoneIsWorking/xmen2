/* One enumeration per directory, instead of one per path component per open. */
#include "host_dir_cache.h"

#include "x2_log.h"

#include <dirent.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/*
 * A directory's names, in one allocation.
 *
 * `names` is a block of NUL-terminated strings and `offsets` indexes into it,
 * so a listing is two allocations however many entries it holds and forgetting
 * one is two frees. Nothing here is shared with a caller except the `const
 * char *` returned by a lookup, which points into `names` and is valid exactly
 * as long as the listing is.
 */
typedef struct Listing {
  struct Listing *next;
  char *path;
  char *names;
  size_t *offsets;
  size_t count;
} Listing;

/* Small and fixed: the install tree has on the order of a hundred directories
   and the point is to avoid the round trip, not to index the disk. A bucket
   holds a chain, so the table never needs to grow or rehash. */
enum { kBuckets = 256u };

static Listing *g_buckets[kBuckets];
static unsigned long g_enumerated, g_hits, g_misses;

/* FNV-1a, pinned to 32 bits rather than to size_t: wasm32's size_t is 32 bits
   and silently truncated the 64-bit constants, so the same path hashed
   differently there than on a 64-bit host. A hash that varies by target is a
   different data structure on each one. */
static size_t hash_path(const char *path) {
  uint32_t hash = 2166136261u;
  for (; *path; ++path) {
    hash ^= (unsigned char)*path;
    hash *= 16777619u;
  }
  return (size_t)(hash % kBuckets);
}

static Listing *find(const char *path) {
  Listing *listing;
  for (listing = g_buckets[hash_path(path)]; listing; listing = listing->next) {
    if (strcmp(listing->path, path) == 0) {
      return listing;
    }
  }
  return NULL;
}

static void destroy(Listing *listing) {
  free(listing->path);
  free(listing->names);
  free(listing->offsets);
  free(listing);
}

/*
 * Read every name in `path` into one listing, or return NULL.
 *
 * NULL means "this directory could not be enumerated", which the caller must
 * treat as "no match" and NOT as "the directory is empty": those differ, and
 * conflating them would resolve a real path to nothing. Nothing is cached on
 * failure, so a directory that appears later is found then.
 */
static Listing *enumerate(const char *path) {
  DIR *directory = opendir(path);
  struct dirent *entry;
  Listing *listing;
  size_t used = 0, capacity = 1024, slots = 0, slot_capacity = 64;
  char *names;
  size_t *offsets;

  if (!directory) {
    return NULL;
  }
  names = malloc(capacity);
  offsets = malloc(slot_capacity * sizeof *offsets);
  listing = calloc(1, sizeof *listing);
  if (!names || !offsets || !listing) {
    free(names);
    free(offsets);
    free(listing);
    closedir(directory);
    x2_log_error("host_dir_cache: out of memory listing \"%s\"\n", path);
    return NULL;
  }
  for (entry = readdir(directory); entry; entry = readdir(directory)) {
    const size_t size = strlen(entry->d_name) + 1u;
    if (used + size > capacity) {
      char *grown;
      while (used + size > capacity) {
        capacity *= 2u;
      }
      grown = realloc(names, capacity);
      if (!grown) {
        break;
      }
      names = grown;
    }
    if (slots == slot_capacity) {
      size_t *grown = realloc(offsets, slot_capacity * 2u * sizeof *offsets);
      if (!grown) {
        break;
      }
      offsets = grown;
      slot_capacity *= 2u;
    }
    memcpy(names + used, entry->d_name, size);
    offsets[slots++] = used;
    used += size;
  }
  closedir(directory);
  listing->path = strdup(path);
  if (!listing->path) {
    free(names);
    free(offsets);
    free(listing);
    x2_log_error("host_dir_cache: out of memory listing \"%s\"\n", path);
    return NULL;
  }
  listing->names = names;
  listing->offsets = offsets;
  listing->count = slots;
  listing->next = g_buckets[hash_path(path)];
  g_buckets[hash_path(path)] = listing;
  g_enumerated++;
  return listing;
}

const char *host_dir_lookup(const char *directory, const char *name,
                            size_t name_size) {
  Listing *listing;
  size_t index;

  if (!directory || !*directory || !name || !name_size) {
    return NULL;
  }
  listing = find(directory);
  if (listing) {
    g_hits++;
  } else {
    g_misses++;
    listing = enumerate(directory);
    if (!listing) {
      return NULL;
    }
  }
  for (index = 0; index < listing->count; ++index) {
    const char *candidate = listing->names + listing->offsets[index];
    if (strlen(candidate) == name_size &&
        strncasecmp(candidate, name, name_size) == 0) {
      return candidate;
    }
  }
  return NULL;
}

void host_dir_forget_for(const char *path) {
  char parent[4096];
  const char *separator;
  size_t size;
  Listing **link;

  if (!path || !*path) {
    return;
  }
  separator = strrchr(path, '/');
  if (!separator) {
    /* A bare name lives in the working directory, which is the one directory
       this cache can never name from the path alone. Forgetting everything is
       the only answer that cannot be wrong, and a relative mutating path is
       rare enough that the cost does not matter. */
    host_dir_clear();
    return;
  }
  size = (size_t)(separator - path);
  if (size == 0) {
    size = 1; /* "/name" -- the parent is the root, not the empty string. */
  }
  if (size >= sizeof parent) {
    host_dir_clear();
    return;
  }
  memcpy(parent, path, size);
  parent[size] = '\0';
  for (link = &g_buckets[hash_path(parent)]; *link; link = &(*link)->next) {
    if (strcmp((*link)->path, parent) == 0) {
      Listing *dead = *link;
      *link = dead->next;
      destroy(dead);
      return;
    }
  }
}

void host_dir_clear(void) {
  size_t bucket;
  for (bucket = 0; bucket < kBuckets; ++bucket) {
    Listing *listing = g_buckets[bucket];
    while (listing) {
      Listing *next = listing->next;
      destroy(listing);
      listing = next;
    }
    g_buckets[bucket] = NULL;
  }
}

void host_dir_cache_stats(unsigned long *enumerated, unsigned long *hits,
                          unsigned long *misses) {
  if (enumerated) {
    *enumerated = g_enumerated;
  }
  if (hits) {
    *hits = g_hits;
  }
  if (misses) {
    *misses = g_misses;
  }
}
