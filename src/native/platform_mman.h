#ifndef X2_PLATFORM_MMAN_H
#define X2_PLATFORM_MMAN_H

#include <sys/mman.h>

/*
 * Linux can request a fixed mapping without replacing an existing one.
 * Darwin has no MAP_FIXED_NOREPLACE, but an address passed without MAP_FIXED
 * is a non-destructive hint: mmap returns that address when the range is free
 * and chooses another range when it is not. Every fixed guest mapping checks
 * the returned address and unmaps a different result, giving this port the
 * same refuse-on-collision contract without MAP_FIXED's destructive overwrite.
 */
#if defined(__APPLE__) && !defined(MAP_FIXED_NOREPLACE)
#define MAP_FIXED_NOREPLACE 0
#endif

#endif
