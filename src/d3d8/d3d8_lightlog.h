/* Byte-comparable D3D8 light-state log shared with the Wine control. */
#ifndef D3D8_LIGHTLOG_H
#define D3D8_LIGHTLOG_H

/* One line: `event t=<ms> ` and then fmt. The time is read only when the log
   is open, so an unlogged call costs a flag test. */
void d3d8_lightlog(const char *event, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
