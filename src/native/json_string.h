#pragma once

#include <cstddef>

namespace x2::native {

/* Format one complete JSON string value, including its quotes. */
int json_string_format(char *out, size_t capacity, const char *value);

} // namespace x2::native
