#ifndef X2_CRT_SELFTEST_H
#define X2_CRT_SELFTEST_H

#include <stdint.h>

typedef void (*CrtSelftestCheck)(const char *name, uint32_t got, uint32_t want);

void crt_selftest_run(uint32_t stack_top, int skip_body, CrtSelftestCheck check);

#endif
