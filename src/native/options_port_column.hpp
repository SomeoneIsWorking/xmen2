#ifndef X2_OPTIONS_PORT_COLUMN_HPP
#define X2_OPTIONS_PORT_COLUMN_HPP

#include "../presentation/retail_scene_plane.hpp"
#include "x86rt.h"

#include <cstdint>
#include <string_view>

/* The right-hand column of the retail Options screen (plan #203): the port's
   rows (port_option_rows.hpp), drawn as cards of the game's option art right
   of the retail panel on desktop and touch alike. The pad and keyboard reach
   them through the retail focus chain of the derived menu; a pointer on the
   column is taken here. */
namespace x2::native {

/* A text item's draw on CMenuOptions: true for a port row, which draws
   nothing itself; the Accept label's draw puts up the whole column and
   returns false so the label draws too. */
bool options_port_column_draw(const CPU &cpu, std::uint32_t self);
/* True when `name` is a port row of CMenuOptions, which the column gives a
   finger-sized card wherever it is shown. */
bool options_port_column_has_card(std::string_view menu_class,
                                  std::string_view name);
/* A text item's destructor: the column's art goes with its first row. */
void options_port_column_destroy(const CPU &cpu, std::uint32_t self);
/* A pointer message on CMenuOptions (0x005cb5f0's arguments): true when
   the column took it. */
bool options_port_column_pointer(const CPU &cpu, std::uint32_t menu,
                                 presentation::ScenePoint point,
                                 std::uint32_t message, std::int32_t wheel);

} // namespace x2::native

#endif
