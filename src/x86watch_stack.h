#ifndef X86WATCH_STACK_H
#define X86WATCH_STACK_H

#include <stdint.h>
#include <stdio.h>

void x86_watch_stack_report(FILE *out, uint32_t entry, uint32_t guest_esp,
                            uint32_t cpu_address, unsigned long cpu_size);

#endif
