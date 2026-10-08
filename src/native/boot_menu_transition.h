#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

/* Invoke the exact retail handler at the end of menus/intro_normal.py. */
int boot_menu_open(const struct X86pCpu *source, uint32_t exe_base);

} // namespace x2::native
