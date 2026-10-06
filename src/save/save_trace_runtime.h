#ifndef X2_SAVE_TRACE_RUNTIME_H
#define X2_SAVE_TRACE_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

/* Runtime wiring for the bounded save evidence collector. The retail function
   overrides are registered by default so an ordinary live run is inspectable.
   X2_SAVE_TRACE=0 disables trace-only wrappers before startup; production
   Continue/autosave wrappers remain registered and call these marker seams,
   which then explicitly report disabled rather than pretending zero events. */
void x2_save_trace_asset_open(const char *guest_path, int succeeded);
void x2_save_trace_menu_open(void);
void x2_save_trace_map_return(uint32_t map, int succeeded);
size_t x2_save_trace_runtime_report(char *out, size_t capacity);
void x2_save_trace_runtime_print(void);

#endif /* X2_SAVE_TRACE_RUNTIME_H */
