#pragma once

#include <cstdint>

namespace x2::native {

/* Build the DirectInput PIDVID product identity presented to the guest. */
void dinput_pad_make_product_guid(unsigned char guid[16], uint16_t vendor,
                                  uint16_t product);

} // namespace x2::native
