#pragma once

namespace x2::web {

// Route the project's existing logger to the browser page before native
// startup.
void install_browser_log_sink();

} // namespace x2::web
