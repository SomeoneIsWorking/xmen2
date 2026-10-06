#ifndef DINPUT_PAD_IDENTITY_H
#define DINPUT_PAD_IDENTITY_H

#include <stdint.h>

/* Build the DirectInput PIDVID product identity presented to the guest. */
void dinput_pad_make_product_guid(unsigned char guid[16], uint16_t vendor,
                                  uint16_t product);

#endif
