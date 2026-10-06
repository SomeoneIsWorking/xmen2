#ifndef X2_AUTOSAVE_FORMAT_H
#define X2_AUTOSAVE_FORMAT_H

#include "autosave_storage.h"

#include <stddef.h>

/* Retail payloads begin with a newline followed by
   [SAVEGAMEBEGIN: <description>]. The save header is that description,
   NUL-padded to the retail 128-byte field. */
int x2_autosave_header_from_payload(const unsigned char *payload,
                                    size_t payload_size,
                                    unsigned char header[X2_SAVE_HEADER_BYTES]);

#endif
