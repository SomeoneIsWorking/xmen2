/* Byte-comparable D3D8 light-state log shared with the Wine control. */
#ifndef D3D8_LIGHTLOG_H
#define D3D8_LIGHTLOG_H

long d3d8_lightlog_ms(void);
void d3d8_lightlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
