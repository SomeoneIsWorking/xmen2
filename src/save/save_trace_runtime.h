#pragma once

#include <cstddef>
#include <cstdint>

namespace x2::save {

/* Runtime wiring for the bounded save evidence collector. The retail function
   overrides are registered by default so an ordinary live run is inspectable.
   X2_SAVE_TRACE=0 disables trace-only wrappers before startup; production
   Continue/autosave wrappers remain registered and call these marker seams,
   which then explicitly report disabled rather than pretending zero events. */
void save_trace_asset_open(const char *guest_path, int succeeded);
void save_trace_menu_open();
void save_trace_map_return(std::uint32_t map, int succeeded);
std::size_t save_trace_runtime_report(char *out, std::size_t capacity);
void save_trace_runtime_print();

} // namespace x2::save
