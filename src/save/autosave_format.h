#pragma once

#include "autosave_storage.h"

#include <cstddef>

namespace x2::save {

/* Retail payloads begin with a newline followed by
   [SAVEGAMEBEGIN: <description>]. The save header is that description,
   NUL-padded to the retail 128-byte field. */
int autosave_header_from_payload(const unsigned char *payload,
                                 size_t payload_size,
                                 unsigned char header[kSaveHeaderBytes]);

} // namespace x2::save
