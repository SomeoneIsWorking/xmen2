#pragma once

#include <cstddef>

namespace x2::input {

/* Render the host controller lifecycle and resolved player ownership into a
   bounded buffer. Returns the bytes present, excluding the terminator. */
std::size_t input_probe_lifecycle_report(char *out, std::size_t n);

} // namespace x2::input
