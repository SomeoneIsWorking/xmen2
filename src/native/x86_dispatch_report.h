/*
 * x86_dispatch_report.h -- explaining a dispatch that found nothing.
 *
 * Split from x86rt_native.c because it is a different job from dispatching:
 * everything here runs exactly once, on the way to abort(), and none of it is
 * on any hot path. Keeping it beside the dispatcher made the dispatcher's own
 * logic harder to read, and the report is the part most often extended -- each
 * addition to it was growing the file that runs every guest call.
 */
#ifndef X2_X86_DISPATCH_REPORT_H
#define X2_X86_DISPATCH_REPORT_H

#include <stdint.h>

struct CPU;

/*
 * Say everything knowable about a dispatch to `target` that found no body:
 * whether the address is an unbound import, which module it falls in, who
 * dispatched there (read from the guest stack, and checked rather than
 * trusted), and the runtime's own diagnostic dump.
 *
 * Does NOT abort. The caller does, so that a reader of the call site can see
 * that the run ends there.
 */
/*
 * One line placing an address: an unbound import slot by name, the module it
 * falls in, or neither -- said explicitly, because "no registered module" and
 * "an import nobody bound" lead to opposite investigations.
 */
void x86_report_where(uint32_t addr);

void x86_report_missing_body(struct CPU *C, uint32_t target);

#endif
