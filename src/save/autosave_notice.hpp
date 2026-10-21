#ifndef X2_AUTOSAVE_NOTICE_HPP
#define X2_AUTOSAVE_NOTICE_HPP

/* The on-screen word after an autosave write; the guest poll sets it and the
 * UI pass reads it. */

#include <chrono>
#include <mutex>
#include <string>

namespace x2::save {

class AutosaveNotice {
public:
  explicit AutosaveNotice(double seconds = 3.0) : seconds_(seconds) {}

  void saved();
  void failed();
  /* The word to draw now, or empty. */
  std::string text() const;

private:
  void show(const char *text);

  double seconds_;
  mutable std::mutex mutex_;
  std::string text_;
  std::chrono::steady_clock::time_point until_{};
};

/* The process's one notice. */
AutosaveNotice &autosave_notice();

} // namespace x2::save

#endif
