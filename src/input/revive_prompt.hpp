#ifndef X2_REVIVE_PROMPT_HPP
#define X2_REVIVE_PROMPT_HPP

/* The paid revive offer and the requests that take it; one mutex joins the host
 * pump and the guest poll. */

#include <chrono>
#include <mutex>
#include <string>

union SDL_Event;

namespace x2::input {

/* Where the prompt is drawn and pressed, as fractions of the window. */
struct PromptRect {
  float left;
  float top;
  float right;
  float bottom;
};
inline constexpr PromptRect kRevivePromptRect{0.30F, 0.13F, 0.70F, 0.22F};

struct RevivePromptView {
  bool offered = false;
  bool affordable = false;
  std::string text;
  /* The last answer (a refusal's reason or a revive's receipt), while fresh. */
  std::string notice;
};

class RevivePrompt {
public:
  explicit RevivePrompt(double notice_seconds = 4.0)
      : notice_seconds_(notice_seconds) {}

  /* The guest poll: what is on offer now. */
  void offer(const std::string &text, bool affordable);
  void withdraw();
  /* The guest poll: the answer to the last request, shown for a while. */
  void announce(const std::string &text);

  /* A request from any input path. True when something was offered. */
  bool request();
  /* The guest poll: take a pending request, once. */
  bool take_request();

  RevivePromptView view() const;

  /* A host event. True means the prompt took it and nothing else may. */
  bool handle_event(const SDL_Event &event);

  /* Testing: forget everything. */
  void reset();

private:
  double notice_seconds_;

  mutable std::mutex mutex_;
  bool offered_ = false;
  bool affordable_ = false;
  bool requested_ = false;
  std::string text_;
  std::string notice_;
  std::chrono::steady_clock::time_point notice_until_{};
  long long finger_ = -1;
};

/* The process's one prompt. */
RevivePrompt &revive_prompt();

/* The host pump's entry, after the settings overlay and before touch. */
bool revive_prompt_event(const SDL_Event &event);

} // namespace x2::input

#endif
