#ifndef X2_PLAYER_PARTICIPATION_PROBE_H
#define X2_PLAYER_PARTICIPATION_PROBE_H

#include <stddef.h>

struct X86pCpu;

size_t x2_player_participation_probe_report(struct X86pCpu *cpu, char *out,
                                            size_t size);

#endif
