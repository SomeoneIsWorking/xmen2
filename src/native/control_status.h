#pragma once

#include <cstddef>

namespace x2::native {

size_t control_status_format(char *body, size_t capacity,
                             unsigned long requests, unsigned long keys_pressed,
                             unsigned long keys_refused,
                             unsigned long screenshots);

} // namespace x2::native
