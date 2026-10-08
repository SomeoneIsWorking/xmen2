#pragma once

#include <cstdint>

struct X86pCpu;

namespace x2::native {

enum class ExactSaveLoadOwner { None, Continue, Menu };

using ExactSaveLoadCompletion = void (*)(int succeeded);

int exact_save_load_read_header(const X86pCpu *source, uint32_t exe,
                                const char *leaf, uint32_t metadata);
int exact_save_load_start(const X86pCpu *source, uint32_t exe, const char *leaf,
                          unsigned staging_slot, ExactSaveLoadOwner owner,
                          ExactSaveLoadCompletion completion);

} // namespace x2::native
