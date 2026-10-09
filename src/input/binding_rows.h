#pragma once

#include <cstdint>

namespace x2::input {

inline constexpr unsigned INPUT_BINDING_ROWS = 42u;

/* The storage key is the executable's registry identifier and must retain its
   exact spelling. The display label is the shipped English PC UI text. */
const char *input_binding_row_storage_key(uint32_t row);
const char *input_binding_row_display_label(uint32_t row);

} // namespace x2::input
