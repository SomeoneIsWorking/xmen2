#ifndef X2_SAVE_TRACE_RUNTIME_H
#define X2_SAVE_TRACE_RUNTIME_H

#include <stddef.h>

/* Runtime wiring for the bounded save evidence collector. The retail function
   overrides are registered by default so an ordinary live run is inspectable.
   X2_SAVE_TRACE=0 disables them before startup; the report remains available
   either way so "disabled" and an observed zero are distinct answers. */
void x2_save_trace_asset_open(const char *guest_path, int succeeded);
size_t x2_save_trace_runtime_report(char *out, size_t capacity);
void x2_save_trace_runtime_print(void);

#endif /* X2_SAVE_TRACE_RUNTIME_H */
