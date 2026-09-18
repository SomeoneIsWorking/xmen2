#ifndef HOST_DIR_CACHE_H
#define HOST_DIR_CACHE_H

#include <stddef.h>

/*
 * The real on-disk spelling of a name, for a case-insensitive lookup that does
 * not re-enumerate the directory every time.
 *
 * WHY THIS EXISTS. The guest asks for Windows paths and the host filesystem is
 * case-sensitive, so every open resolves each path component by listing its
 * directory and comparing case-insensitively. On a desktop that is a handful
 * of cached `getdents` calls. In the browser it is not: WASMFS runs the
 * filesystem on another thread, so each enumeration is a cross-thread round
 * trip and the caller sits in `emscripten_futex_wait` for all of them.
 * Measured in a browser during the Dead Zone route:
 * `MSVCRT.dll!fopen: 513.4 ms in 22 call(s)` -- 23 ms per open, while the
 * whole interval's guest execution was 809 ms. The cost is not the comparing,
 * it is doing the enumeration at all, once per component, once per open.
 *
 * So a directory is enumerated once and its names are kept. A lookup then
 * costs a scan of memory this process already has.
 *
 * THE INVALIDATION CONTRACT. A cached listing is only correct while the
 * directory's set of names is unchanged, and this cache cannot see changes
 * made behind its back. Every place in this port that creates, removes or
 * renames a name calls `host_dir_forget_for` with the path it touched, which
 * drops the listing for that path's parent. The cache is deliberately not
 * time-based and does not re-stat: a stale entry would resolve a real file to
 * a name that no longer exists, which is worse than the cost it saves. If a
 * directory changes through some path that does not call `host_dir_forget_for`,
 * that is a defect in the caller, not something for this owner to paper over.
 */

/* The on-disk name in `directory` matching the first `name_size` bytes of
 * `name` case-insensitively, or NULL if the directory holds no such name or
 * cannot be read. The returned pointer is owned by this cache and stays valid
 * until that directory is forgotten. */
const char *host_dir_lookup(const char *directory, const char *name,
                            size_t name_size);

/* Drop the cached listing for `path`'s parent directory, because a name in it
 * was just created, removed or renamed. Safe to call with a path whose parent
 * was never cached. */
void host_dir_forget_for(const char *path);

/* Drop every cached listing. For a host that has replaced the install tree
 * under a running process, and for tests. */
void host_dir_clear(void);

/* Directories enumerated, lookups answered from memory, and lookups that had
 * to enumerate. A run where `enumerated` keeps climbing with `hits` is a
 * cache that is being invalidated more often than it is used, which is worth
 * knowing and is not visible from a timing alone. */
void host_dir_cache_stats(unsigned long *enumerated, unsigned long *hits,
                          unsigned long *misses);

#endif /* HOST_DIR_CACHE_H */
