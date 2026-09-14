#pragma once

namespace x2::web {

// Route the project's existing logger to the browser page before native
// startup.
void install_browser_log_sink();

// Hand over anything still batched. The sink holds lines back to keep one
// synchronous round trip per block instead of per line, so a run that is about
// to stop -- and the page about to unload it -- has to ask for the tail.
void flush_browser_log();

} // namespace x2::web
