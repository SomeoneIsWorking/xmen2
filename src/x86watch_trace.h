#ifndef X86WATCH_TRACE_H
#define X86WATCH_TRACE_H

#include <stdint.h>
#include <stdio.h>

void x86_watch_trace_reset(void);
void x86_watch_trace_note(int kind, uint32_t address, uint32_t esp,
                          unsigned long thread_id);
void x86_watch_trace_dump(FILE *out);

#endif
