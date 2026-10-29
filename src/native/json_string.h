#pragma once

#include <cstddef>
#include <string>

namespace x2::native {

/* Format one complete JSON string value, including its quotes. */
int json_string_format(char *out, size_t capacity, const char *value);

/* The same value as a std::string. */
std::string json_string(const std::string &value);

} // namespace x2::native
