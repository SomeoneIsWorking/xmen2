#include "rml_text.hpp"

namespace x2::ui {

std::string escape_rml(const std::string &text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (c == '&')
      out += "&amp;";
    else if (c == '<')
      out += "&lt;";
    else if (c == '>')
      out += "&gt;";
    else if (c == '\"')
      out += "&quot;";
    else
      out += c;
  }
  return out;
}

} // namespace x2::ui
