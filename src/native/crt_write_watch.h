#ifndef X2_CRT_WRITE_WATCH_H
#define X2_CRT_WRITE_WATCH_H

#include <stdint.h>

/* Publish the watched dword after a native CRT bulk write that fully covers
   it. Ordinary JIT guest stores are handled by the x86 runtime helpers. */
void crt_write_watch_dst(uint32_t destination, uint32_t size);

#endif /* X2_CRT_WRITE_WATCH_H */
