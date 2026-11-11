#ifndef X2_DERIVED_MENUS_HPP
#define X2_DERIVED_MENUS_HPP

#include "xmlb_tree.hpp"

#include <optional>
#include <string>

/* Retail menu files the port adds rows to. Each is derived from the
   player's own install into the save directory on first open and served in
   its place, so every platform gets the same rows; a file that cannot be
   derived opens as shipped and the failure is logged. */
namespace x2::native {

/* The host path to open for `guest_path` instead of the install's file, or
   null when the port derives nothing for it. */
const char *derived_menu_replacement(const char *guest_path);

} // namespace x2::native

#endif
