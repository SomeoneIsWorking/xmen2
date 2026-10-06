/*
 * The port installs its own sink for x86port's diagnostics because the
 * library's default writes to standard error, and no host of this port reads
 * standard error: in the browser a worker's never reaches the page at all.
 * A stop reported that way says "native code called abort()" and nothing
 * about why. So the whole contract under test is that the message ARRIVES,
 * with the component and the text the library passed.
 *
 * x86_diag_dump() is stubbed rather than linked: the sink's job on a fatal
 * diagnostic is to call it before the library aborts, and pulling the whole
 * engine in to watch that would test the engine instead of the wiring.
 */
#include "x86_engine_diagnostic.h"

#include "x2_log.h"

#include <lucent/log.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "diagnostic.h"

namespace {

std::vector<std::string> g_lines;
int g_dumps;

bool contains(const std::string &line, const char *needle) {
  return line.find(needle) != std::string::npos;
}

int fail(const char *what, std::size_t got) {
  std::fprintf(stderr, "test_x86_engine_diagnostic: %s (%zu line(s))\n", what,
               got);
  for (const std::string &line : g_lines) {
    std::fprintf(stderr, "    %s\n", line.c_str());
  }
  return 1;
}

} // namespace

extern "C" void x86_diag_dump(void) { ++g_dumps; }

int main() {
  lucent::set_sink([](lucent::Level, std::string_view line) {
    g_lines.push_back(std::string(line));
  });
  x86_engine_diagnostic_install();

  {
    const X86pDiagnostic diagnostic = {kX86pDiagnosticError, "unit",
                                       "an error the run has to be able to "
                                       "read"};
    x86p_diagnostic_report(&diagnostic);
  }
  if (g_lines.size() != 1 || !contains(g_lines[0], "x86port[unit]") ||
      !contains(g_lines[0], "an error the run has to be able to read") ||
      g_dumps != 0) {
    return fail("an error diagnostic did not reach the port's logger",
                g_lines.size());
  }

  {
    const X86pDiagnostic diagnostic = {kX86pDiagnosticFatal, "jit",
                                       "a violated contract"};
    x86p_diagnostic_report(&diagnostic);
  }
  if (g_lines.size() != 2 || !contains(g_lines[1], "fatal") ||
      !contains(g_lines[1], "x86port[jit]") || g_dumps != 1) {
    return fail("a fatal diagnostic did not report and dump", g_lines.size());
  }

  /* Both fields are optional in the library's struct; the sink still has to
     say something rather than print a null. */
  {
    const X86pDiagnostic diagnostic = {kX86pDiagnosticError, nullptr, nullptr};
    x86p_diagnostic_report(&diagnostic);
    x86p_diagnostic_report(nullptr);
  }
  if (g_lines.size() != 4 || !contains(g_lines[2], "x86port[library]") ||
      !contains(g_lines[2], "unspecified diagnostic") ||
      !contains(g_lines[3], "unspecified diagnostic")) {
    return fail("an unnamed diagnostic did not get a default", g_lines.size());
  }

  /* The NEGATIVE control, and the reason this file exists: the library's own
     sink must NOT be what carries these. Restoring it sends the report to
     standard error, which is exactly the silence being fixed. */
  x86p_diagnostic_set_sink(nullptr, nullptr);
  {
    const X86pDiagnostic diagnostic = {kX86pDiagnosticError, "unit",
                                       "after the default sink was restored"};
    x86p_diagnostic_report(&diagnostic);
  }
  if (g_lines.size() != 4) {
    return fail("the library's default sink is reaching the port's logger",
                g_lines.size());
  }

  x86_engine_diagnostic_install();
  {
    const X86pDiagnostic diagnostic = {kX86pDiagnosticError, "unit",
                                       "installed a second time"};
    x86p_diagnostic_report(&diagnostic);
  }
  lucent::set_sink(nullptr);
  if (g_lines.size() != 5 || !contains(g_lines[4], "installed a second time")) {
    return fail("reinstalling the sink did not route again", g_lines.size());
  }
  return 0;
}
