#pragma once

#include <cstdint>

namespace x2::native {

using CrtSelftestCheck = void (*)(const char *name, uint32_t got,
                                  uint32_t want);

void crt_selftest_run(uint32_t stack_top, int skip_body,
                      CrtSelftestCheck check);

} // namespace x2::native
