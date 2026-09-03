#include "autosave_format.h"

#include <string.h>

int x2_autosave_header_from_payload(
    const unsigned char *payload, size_t payload_size,
    unsigned char header[X2_SAVE_HEADER_BYTES]) {
  static const unsigned char PREFIX[] = "[SAVEGAMEBEGIN: ";
  const unsigned char *description;
  size_t description_size = 0;
  size_t offset;

  if (!payload || !header || payload_size < sizeof PREFIX + 2u ||
      payload[0] != '\n' || memcmp(payload + 1u, PREFIX, sizeof PREFIX - 1u))
    return 0;
  offset = sizeof PREFIX;
  description = payload + offset;
  while (offset + description_size < payload_size &&
         payload[offset + description_size] != ']') {
    unsigned char ch = payload[offset + description_size];
    if (ch < 0x20u || ch > 0x7eu ||
        description_size >= X2_SAVE_HEADER_BYTES - 1u)
      return 0;
    description_size++;
  }
  if (!description_size || offset + description_size >= payload_size)
    return 0;
  memset(header, 0, X2_SAVE_HEADER_BYTES);
  memcpy(header, description, description_size);
  return 1;
}
