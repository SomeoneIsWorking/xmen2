#ifndef X2_UI_RESOURCES_H
#define X2_UI_RESOURCES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Resolve a packaged UI resource without baking a build-tree path into the
 * release. X2_UI_RESOURCE_DIR is supplied by AppRun; the CMake build
 * directory remains the developer fallback. */
const char *x2_ui_resource_path(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* X2_UI_RESOURCES_H */
