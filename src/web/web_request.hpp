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
 * The gameplay test can be pointed at another map.
 *
 * The Dead Zone map does not draw the retail party HUD -- measured, on both
 * the native and the browser products: 0 visible party draws against the
 * tutorial map's 19 on the same direct-load route. The touch overlay is
 * gated on that HUD, on purpose, so the Dead Zone route can never show an
 * on-screen control and touch cannot be exercised through it. A maintainer
 * needs to reach a map that does.
 *
 * The map lives here rather than in web_main.cpp so the default and the
 * override have one home; the Android bridge already takes its boot map from
 * its caller for the same reason.
 */
inline constexpr char gameplay_map_prefix[] = "--test-map=";
inline constexpr char deadzone_map[] = "act1/deadzone/deadzone1";

/*
 * Scan every argument, not just argv[1]: the page appends its `?arg=`
 * diagnostics after the request, so the request is frequently not the last
 * token. A previous test keyed on `argc == 2` and silently dropped the
 * gameplay test as soon as the URL carried a diagnostic, booting the retail
 * intro while looking like a stall (issue #151).
 */
launch_request classify_launch_request(int argc, char **argv);

/* The map the gameplay test should boot: the last `--test-map=` value, or the
 * Dead Zone default. Never empty -- an empty value is refused by returning
 * the default, because booting "no map" is not a thing the runtime can do and
 * a silently empty boot map looks exactly like a hung load. */
const char *gameplay_test_map(int argc, char **argv);

/* Whether argv[i] names a request this entry point consumes (and must
 * therefore strip before forwarding the runtime's own arguments). */
bool is_entry_request(const char *arg);

} // namespace x2::web
