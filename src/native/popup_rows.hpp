#ifndef X2_POPUP_ROWS_HPP
#define X2_POPUP_ROWS_HPP

#include "../presentation/retail_scene_plane.hpp"
#include "retail_menu_model.hpp"

#include <vector>

/* CPopupDialog's option rows (docs/RE/menus.md): on touch each option keeps
   its retail text line and owns a slot at least a finger tall. */
namespace x2::native {

/* The slot of an option whose text line is `line` scene units tall. */
float popup_row_slot(float line, const presentation::RetailScenePlane &plane);

/* Where the last popup draw put each shown option, in draw order from the
   first shown entry: the box CPopupDialog::onMouse hit-tests for it. */
class PopupRowsDrawn {
public:
  void begin() { rows_.clear(); }
  void add(const menu::SceneRect &row) { rows_.push_back(row); }
  const std::vector<menu::SceneRect> &rows() const { return rows_; }

private:
  std::vector<menu::SceneRect> rows_;
};

/* Written by the popup draw on the guest thread, read there by GET /menu. */
PopupRowsDrawn &popup_rows_drawn();

} // namespace x2::native

#endif
