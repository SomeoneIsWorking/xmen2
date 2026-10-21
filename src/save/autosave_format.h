#pragma once

#include "autosave_storage.h"

#include <cstddef>

namespace x2::save {

/* The tag field 0x00468990 writes after the payload's leading newline. */
inline constexpr size_t kSaveTagBytes = 0x32u;

/* Retail payloads begin with a newline followed by
   [SAVEGAMEBEGIN: <description>] in the tag field, cut short with a NUL when
   the description does not fit. The save header is that description,
   NUL-padded to the retail 128-byte field. */
int autosave_header_from_payload(const unsigned char *payload,
                                 size_t payload_size,
                                 unsigned char header[kSaveHeaderBytes]);

} // namespace x2::save
