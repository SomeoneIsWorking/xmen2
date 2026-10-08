#pragma once

namespace x2::native {

/* Check the on-disk header accepted by the x86 PE loader.  This deliberately
 * does not map or execute the image; install selection uses it to refuse a
 * corrupt or non-x86 file before promoting it into private storage. */
int pe32_validate_file(const char *path, char *reason,
                       unsigned reason_capacity);

} // namespace x2::native
