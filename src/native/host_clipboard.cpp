#include "host_clipboard.hpp"

#include "x2_log.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_stdinc.h>

namespace x2::native {

bool clipboard_put(std::string_view text) {
  const std::string owned(text);
  if (!SDL_SetClipboardText(owned.c_str())) {
    x2_log_error("clipboard: SDL could not take the text: %s\n",
                 SDL_GetError());
    return false;
  }
  return true;
}

std::optional<std::string> clipboard_text() {
  if (!SDL_HasClipboardText()) {
    return std::nullopt;
  }
  char *text = SDL_GetClipboardText();
  if (text == nullptr) {
    x2_log_error("clipboard: SDL could not read the text: %s\n",
                 SDL_GetError());
    return std::nullopt;
  }
  std::string out(text);
  SDL_free(text);
  return out;
}

} // namespace x2::native
