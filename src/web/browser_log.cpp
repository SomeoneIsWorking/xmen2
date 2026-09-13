#include "browser_log.hpp"

#include <emscripten/em_asm.h>
#include <lucent/log.h>

#include <string>
#include <string_view>

namespace x2::web {

void install_browser_log_sink() {
  lucent::set_sink([](lucent::Level level, std::string_view line) {
    std::string message(line);
    MAIN_THREAD_EM_ASM(
        {
          const message = UTF8ToString($0);
          if ($1 >= 3) {
            console.error(message);
          } else if ($1 == 2) {
            console.warn(message);
          } else {
            console.log(message);
          }
        },
        message.c_str(), static_cast<int>(level));
  });
}

} // namespace x2::web
