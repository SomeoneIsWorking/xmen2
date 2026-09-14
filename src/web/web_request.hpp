#pragma once

#include <cstddef>

namespace x2::web {

/*
 * The launch request the web entry point itself owns.
 *
 * `--import` (unpack a freshly chosen ZIP) and `--test-deadzone` (boot the
 * diagnostic Dead Zone map) are consumed by web_main.cpp, not forwarded to the
 * runtime. Every other argument is the runtime's. This is a pure function of
 * argv so the routing rule is unit-tested without a browser or an Emscripten
 * runtime.
 */
enum class launch_request {
  none, /* neither request was named; boot the saved install normally */
  import_archive, /* `--import` */
  gameplay_test,  /* `--test-deadzone` */
  conflict,       /* both were named; they select different routes */
};

/* The argv value that names each request. web_main and the forwarding filter
 * share these so the request cannot be consumed under one name and forwarded
 * under another. */
inline constexpr char import_flag[] = "--import";
inline constexpr char gameplay_test_flag[] = "--test-deadzone";

/*
 * Scan every argument, not just argv[1]: the page appends its `?arg=`
 * diagnostics after the request, so the request is frequently not the last
 * token. A previous test keyed on `argc == 2` and silently dropped the
 * gameplay test as soon as the URL carried a diagnostic, booting the retail
 * intro while looking like a stall (issue #151).
 */
launch_request classify_launch_request(int argc, char **argv);

/* Whether argv[i] names a request this entry point consumes (and must
 * therefore strip before forwarding the runtime's own arguments). */
bool is_entry_request(const char *arg);

} // namespace x2::web
