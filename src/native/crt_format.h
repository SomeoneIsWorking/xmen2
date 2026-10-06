#ifndef X2_NATIVE_CRT_FORMAT_H
#define X2_NATIVE_CRT_FORMAT_H

#include <stddef.h>
#include <stdint.h>

/* printf over a guest x86-32 va_list; returns the length that would have been
 * written. */
int guest_vformat(char *out, size_t cap, const char *fmt, uint32_t va);
/* sscanf storing through guest pointers; returns the number of fields filled.
 */
int guest_vsscanf(const char *in, const char *fmt, uint32_t va);

#endif
