/* Android ships the game runner, not the desktop-only oracle instrumentation.
 * Keep the shared reporting call sites linked and make the unsupported
 * maintainer operation explicit to callers. */
#include "oracle_trace.h"
#include "guest_memory.h"

int probe_page_readable(unsigned int page)
{
    return guest_memory_is_readable(page, 4096u);
}

void oracle_probe_arm(void) {}

int oracle_probe_line(char *buf, int n)
{
    (void)buf;
    (void)n;
    return 0;
}

void oracle_probe_report(void) {}

int oracle_probe_selftest(void)
{
    return 77;
}
