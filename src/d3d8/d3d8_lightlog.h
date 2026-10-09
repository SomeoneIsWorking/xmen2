/* Byte-comparable D3D8 light-state log shared with the Wine control. */
#pragma once

namespace x2::d3d8 {

/* One line: `event t=<ms> ` and then fmt. The time is read only when the log
   is open, so an unlogged call costs a flag test. */
void d3d8_lightlog(const char *event, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

} // namespace x2::d3d8
