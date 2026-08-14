#ifndef X2RUN_DIAG_H
#define X2RUN_DIAG_H
#include <stdint.h>
void x2run_diag_start(void);
void x2run_diag_note(const char *what, uint32_t value);
#endif
