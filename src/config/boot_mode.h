#pragma once

namespace x2::config {

enum class BootMode : int { Normal = 0, Menu, Continue };

const char *boot_mode_name(BootMode mode);
const char *boot_mode_label(BootMode mode);
int boot_mode_parse(const char *text, BootMode *mode);

} // namespace x2::config
