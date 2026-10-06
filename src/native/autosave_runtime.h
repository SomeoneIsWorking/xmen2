#ifndef X2_AUTOSAVE_RUNTIME_H
#define X2_AUTOSAVE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

struct X86pCpu;

void x2_autosave_runtime_map_return(int succeeded);
void x2_autosave_runtime_menu_show(void);
void x2_autosave_runtime_poll(struct X86pCpu *cpu);
size_t x2_autosave_runtime_report(char *out, size_t capacity);

#endif
