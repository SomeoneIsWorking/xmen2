/* Finding an export by name in a PE image's sorted export name table. */
#pragma once

#include <cstdint>

namespace x2::native {

/*
 * The index in the export name pointer table of `name`, or -1.
 *
 * `image` is the whole mapped image, `names` the RVA of its name pointer
 * table and `count` its entries. The PE format orders that table by byte
 * value so a loader can binary-search it, and the Windows loader does; the
 * title's imports number about five thousand against tables of up to eleven
 * thousand names, so a linear scan was twenty-two million string compares at
 * boot.
 */
long pe_export_name_index(const unsigned char *image, uint32_t names,
                          uint32_t count, const char *name);

} // namespace x2::native
