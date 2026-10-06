#pragma once

#include <cstddef>
#include <cstdint>

struct X86pCpu;

namespace x2::native {

void autosave_runtime_map_return(int succeeded);
void autosave_runtime_menu_show();
void autosave_runtime_poll(struct X86pCpu *cpu);
std::size_t autosave_runtime_report(char *out, std::size_t capacity);

} // namespace x2::native
