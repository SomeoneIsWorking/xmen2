/*
 * The web entry point finds its launch request wherever it sits in argv.
 *
 * The falsifier is the exact browser command line from issue #151: the page
 * puts `--test-deadzone` first and appends `?arg=` diagnostics after it. The
 * previous `argv[1]`-and-`argc == 2` test classified that line as "no
 * request", so the run silently booted the retail intro at the same guest
 * progress point every time while looking like a stall.
 */
#include "web_request.hpp"

#include <cstdio>

namespace {

int failures = 0;

void check(const char *what, x2::web::launch_request got,
           x2::web::launch_request want) {
  if (got != want) {
    std::printf("FAIL: %s classified as %d, expected %d\n", what,
                static_cast<int>(got), static_cast<int>(want));
    ++failures;
  }
}

x2::web::launch_request classify(const char *const *args, int argc) {
  /* classify_launch_request never writes argv. */
  return x2::web::classify_launch_request(
      argc, const_cast<char **>(const_cast<char *const *>(args)));
}

} // namespace

int main() {
  const char *bare[] = {"x2native"};
  check("no arguments", classify(bare, 1), x2::web::launch_request::none);

  const char *import[] = {"x2native", "--import"};
  check("import alone", classify(import, 2),
        x2::web::launch_request::import_archive);

  const char *test[] = {"x2native", "--test-deadzone"};
  check("gameplay test alone", classify(test, 2),
        x2::web::launch_request::gameplay_test);

  /* The issue #151 command line, exactly as the page builds it. */
  const char *test_with_diag[] = {"x2native", "--test-deadzone", "--set",
                                  "quantum=20000"};
  check("gameplay test plus a page diagnostic", classify(test_with_diag, 4),
        x2::web::launch_request::gameplay_test);

  const char *import_with_diag[] = {"x2native", "--import", "--set",
                                    "hotep=4096"};
  check("import plus a page diagnostic", classify(import_with_diag, 4),
        x2::web::launch_request::import_archive);

  const char *late[] = {"x2native", "--set", "quantum=4000", "--test-deadzone"};
  check("request after a diagnostic", classify(late, 4),
        x2::web::launch_request::gameplay_test);

  const char *conflict[] = {"x2native", "--import", "--test-deadzone"};
  check("both requests", classify(conflict, 3),
        x2::web::launch_request::conflict);

  const char *junk[] = {"x2native", "--test-deadzones", "--impor"};
  check("near-miss tokens are not requests", classify(junk, 3),
        x2::web::launch_request::none);

  if (!x2::web::is_entry_request("--import") ||
      !x2::web::is_entry_request("--test-deadzone") ||
      x2::web::is_entry_request("--set") ||
      x2::web::is_entry_request("quantum=20000")) {
    std::printf("FAIL: the forwarding filter names the wrong tokens\n");
    ++failures;
  }

  if (failures == 0) {
    std::printf("web_request: launch routing classified all cases correctly\n");
    return 0;
  }
  std::printf("web_request: %d check(s) FAILED\n", failures);
  return 1;
}
