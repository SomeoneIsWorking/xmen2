#ifndef X2_SEBAS_TOUCH_LAYOUT_HPP
#define X2_SEBAS_TOUCH_LAYOUT_HPP

#include <optional>
#include <string_view>
#include <utility>
#include <vector>

/* Advanced Options (CMenuSebas) on touch (docs/touch-play.md): where its
   widgets go, in the window's 640x480 layout units, and the finger's read
   of its slider and binding table. */
namespace x2::input {

/* A widget's layout as FUN_0061fe20 takes it: x, y, width, height. */
struct SebasBox {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  bool operator==(const SebasBox &) const = default;
};

/* A button's height in layout units: the window keeps 4:3 at the client's
   height (480 units) and a finger is a tenth of it, 48; two more keep the
   rounded client rectangle a whole finger. */
inline constexpr int kSebasButton = 50;

/* Where a widget goes on touch, and the text it shows there when that
   differs (the footer's `Esc Back` names a key a finger has not got). */
struct SebasPlace {
  SebasBox retail;
  SebasBox touch;
  std::string_view text;
};

/* The touch place for the widget FUN_0061dc10 built at `retail`, nullopt
   for a widget that keeps its place: FSAA, the tab bar and its tabs, the
   Defaults bar and its buttons, and the footer's Back and Revert become
   finger tall. */
std::optional<SebasPlace> sebas_touch_place(SebasBox retail);

/* The kinds of widget a confirmation dialog (FUN_0061c740, FUN_0061cbf0,
   FUN_0061d0a0, FUN_0061d760) is laid out by: its answer rows, the
   texs\selected.png bar behind the chosen one and its `$MENU_BACK Back`. */
enum class SebasDialogRole : int { row, highlight, back, other };

/* The text a dialog's Back shows on touch, its words without the key. */
inline constexpr std::string_view kSebasBackText = "Back";
/* Layout units between two answer rows. */
inline constexpr int kSebasRowGap = 6;

/* A dialog's widgets in their touch places, in the order given: the rows
   side by side in one finger row across the first row's width, the bar
   over the first of them, everything else where it was. */
std::vector<SebasBox> sebas_dialog_boxes(
    const std::vector<std::pair<SebasDialogRole, SebasBox>> &widgets);

/* The client y a press inside a finger band around the slider is delivered
   at: its centre line while `y` is within half a finger of it, nullopt
   otherwise. `finger` is a finger in client pixels. */
std::optional<int> sebas_slider_y(int top, int bottom, int y, int finger);

/* Whole wheel notches a drag of `travel` client pixels down the binding
   table has moved past, for rows `row` pixels apart; positive scrolls the
   table toward its first row. */
int sebas_table_notches(int travel, int row);

} // namespace x2::input

#endif
