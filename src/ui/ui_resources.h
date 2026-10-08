#pragma once

namespace x2::ui {

/* Resolve a packaged UI resource without baking a build-tree path into the
 * release. X2_UI_RESOURCE_DIR is supplied by AppRun; the CMake build
 * directory remains the developer fallback. */
const char *ui_resource_path(const char *name);

} // namespace x2::ui
