#include "control_console.h"

#include "retail_front_end.hpp"

extern "C" {
#include "control_query.h"
}

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace x2::control {
namespace {

inline constexpr size_t kCommandBytes = 128u;
inline constexpr std::chrono::seconds kWait{10};

/* HTTP query values arrive raw; a console command needs its spaces back. */
std::string unescape(const char *text) {
  std::string out;
  for (const char *in = text; *in; ++in) {
    if (*in == '+') {
      out += ' ';
    } else if (in[0] == '%' && in[1] && in[2]) {
      const char hex[3] = {in[1], in[2], 0};
      out += static_cast<char>(std::strtol(hex, nullptr, 16));
      in += 2;
    } else {
      out += *in;
    }
  }
  return out;
}

/* One command in flight, handed from the server thread to the guest thread. */
class ConsoleChannel {
public:
  enum class Outcome { Queued, Refused, Busy, TimedOut };

  Outcome submit(const std::string &command) {
    std::unique_lock lock(mutex_);
    if (state_ != State::Idle) {
      return Outcome::Busy;
    }
    command_ = command;
    state_ = State::Pending;
    done_.wait_for(lock, kWait, [this] { return state_ == State::Done; });
    const bool reached = state_ == State::Done;
    state_ = State::Idle;
    if (!reached) {
      return Outcome::TimedOut;
    }
    return queued_ ? Outcome::Queued : Outcome::Refused;
  }

  void pump(CPU *cpu) {
    std::lock_guard lock(mutex_);
    if (state_ != State::Pending) {
      return;
    }
    queued_ = queue(cpu);
    state_ = State::Done;
    done_.notify_one();
  }

private:
  enum class State { Idle, Pending, Done };

  bool queue(CPU *cpu) {
    return retail::FrontEnd::queue_command(*cpu, command_);
  }

  std::mutex mutex_;
  std::condition_variable done_;
  State state_ = State::Idle;
  std::string command_;
  bool queued_ = false;
};

ConsoleChannel g_channel;

ConsoleChannel &channel() { return g_channel; }

} // namespace
} // namespace x2::control

extern "C" void control_console_pump(CPU *cpu) {
  x2::control::channel().pump(cpu);
}

extern "C" void control_console_route(x2_socket_t fd, const char *query) {
  using Outcome = x2::control::ConsoleChannel::Outcome;
  std::array<char, x2::control::kCommandBytes> raw{};
  if (!control_query_arg(query, "command", raw.data(), raw.size()) || !raw[0]) {
    control_reply_text(fd, 400, "Bad Request",
                       "no command. Use /console?command=openmenu+online\n");
    return;
  }
  const std::string command = x2::control::unescape(raw.data());
  switch (x2::control::channel().submit(command)) {
  case Outcome::Queued:
    control_reply_text(fd, 200, "OK",
                       "queued \"%s\"; the console runs it at its next safe "
                       "point and logs a name it does not know\n",
                       command.c_str());
    break;
  case Outcome::Refused:
    control_reply_text(fd, 409, "Conflict",
                       "the console refused \"%s\": it is shutting down\n",
                       command.c_str());
    break;
  case Outcome::Busy:
    control_reply_text(fd, 409, "Conflict",
                       "an earlier console command is still waiting for the "
                       "guest's input poll\n");
    break;
  case Outcome::TimedOut:
    control_reply_text(fd, 504, "Gateway Timeout",
                       "the guest did not reach an input poll within 10s; "
                       "\"%s\" was not run\n",
                       command.c_str());
    break;
  }
}
