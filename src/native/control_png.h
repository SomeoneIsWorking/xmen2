#ifndef X2_CONTROL_PNG_H
#define X2_CONTROL_PNG_H

#include <stddef.h>

/* BGRA8 pixels -> a malloc'd PNG the caller frees. NULL on a zero-sized image
   or an allocation failure. See control_png.c for why this is not a library. */
unsigned char *control_png_from_bgra(const unsigned char *bgra,
                                     unsigned w, unsigned h, size_t *out_len);

#endif
