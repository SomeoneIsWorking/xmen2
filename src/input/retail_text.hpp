#ifndef X2_RETAIL_TEXT_HPP
#define X2_RETAIL_TEXT_HPP

#include <string>
#include <string_view>

namespace x2::input {

/* Retail text as a player reads it: style escapes ("~05") and prompt tokens
   ("$MENU_BACK") removed, whitespace collapsed, and letter-spaced titles
   ("r i s e   o f") closed up. */
std::string retail_shown_text(std::string_view raw);

} // namespace x2::input

#endif
