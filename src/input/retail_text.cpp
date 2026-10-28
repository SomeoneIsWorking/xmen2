#include "retail_text.hpp"

#include <cctype>

namespace x2::input {
namespace {

constexpr std::string_view kPromptToken = "$MENU_";

bool is_space(char c) { return std::isspace(static_cast<unsigned char>(c)); }

bool is_digit(char c) { return std::isdigit(static_cast<unsigned char>(c)); }

/* "r i s e   o f" -> "rise of": every other character a single space. */
bool letter_spaced(std::string_view text) {
  if (text.size() < 3u) {
    return false;
  }
  for (std::size_t i = 1; i < text.size(); i += 2u) {
    if (text[i] != ' ') {
      return false;
    }
  }
  return true;
}

std::string close_letter_spacing(std::string_view text) {
  std::string out;
  std::size_t spaces = 0;
  for (const char c : text) {
    if (c == ' ') {
      ++spaces;
      continue;
    }
    if (spaces >= 3u && !out.empty()) {
      out.push_back(' ');
    }
    spaces = 0;
    out.push_back(c);
  }
  return out;
}

} // namespace

std::string retail_shown_text(std::string_view raw) {
  std::string kept;
  for (std::size_t i = 0; i < raw.size();) {
    if (raw[i] == '~' && i + 2u < raw.size() && is_digit(raw[i + 1u]) &&
        is_digit(raw[i + 2u])) {
      i += 3u;
      continue;
    }
    if (raw.substr(i, 2u) == "~~") {
      i += 2u;
      continue;
    }
    /* A $MENU_ token is a button prompt; any other "$HP" draws as "HP". */
    if (raw.substr(i, kPromptToken.size()) == kPromptToken) {
      while (i < raw.size() && !is_space(raw[i])) {
        ++i;
      }
      continue;
    }
    if (raw[i] == '$') {
      ++i;
      continue;
    }
    kept.push_back(raw[i]);
    ++i;
  }
  std::size_t first = 0;
  while (first < kept.size() && is_space(kept[first])) {
    ++first;
  }
  std::size_t last = kept.size();
  while (last > first && is_space(kept[last - 1u])) {
    --last;
  }
  const std::string_view trimmed(kept.data() + first, last - first);
  if (letter_spaced(trimmed)) {
    return close_letter_spacing(trimmed);
  }
  std::string out;
  bool space = false;
  for (const char c : trimmed) {
    if (is_space(c)) {
      space = true;
      continue;
    }
    if (space) {
      out.push_back(' ');
      space = false;
    }
    out.push_back(c);
  }
  return out;
}

} // namespace x2::input
