#include "autosave_notice.hpp"

namespace x2::save {
namespace {

AutosaveNotice notice;

} // namespace

void AutosaveNotice::show(const char *text) {
  const std::lock_guard<std::mutex> lock(mutex_);
  text_ = text;
  until_ = std::chrono::steady_clock::now() +
           std::chrono::duration_cast<std::chrono::steady_clock::duration>(
               std::chrono::duration<double>(seconds_));
}

void AutosaveNotice::saved() { show("Autosaved"); }

void AutosaveNotice::failed() { show("Autosave failed"); }

std::string AutosaveNotice::text() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  if (std::chrono::steady_clock::now() >= until_) {
    return {};
  }
  return text_;
}

AutosaveNotice &autosave_notice() { return notice; }

} // namespace x2::save
