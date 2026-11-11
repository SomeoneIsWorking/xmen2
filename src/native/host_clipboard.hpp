#ifndef X2_HOST_CLIPBOARD_HPP
#define X2_HOST_CLIPBOARD_HPP

#include <optional>
#include <string>
#include <string_view>

/* The player's system clipboard, through SDL: the desktop's, Android's or
   the browser's. */
namespace x2::native {

/* False when the platform refused the text, and the reason is logged. */
bool clipboard_put(std::string_view text);
/* The clipboard's text; nothing when it holds no text. */
std::optional<std::string> clipboard_text();

} // namespace x2::native

#endif
