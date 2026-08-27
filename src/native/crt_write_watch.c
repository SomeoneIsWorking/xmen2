#include "crt_write_watch.h"

#include "x86rt.h"

void crt_write_watch_dst(uint32_t destination, uint32_t size)
{
    uint32_t watched = x2_write_watch_addr;
    uint64_t end = (uint64_t)destination + size;

    if (watched && destination <= watched && (uint64_t)watched + 4u <= end)
        x2_write_watch_fire(watched, RD32(watched));
}
