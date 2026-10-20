#include "control_console.h"

#include "retail_front_end.hpp"

#include "control_query.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace x2::control {
namespace {

inline constexpr std::chrono::seconds kWait{10};

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

namespace x2::native {

void control_console_pump(CPU *cpu) { x2::control::channel().pump(cpu); }

void control_console_run(Socket fd, const std::string &command) {
  using Outcome = x2::control::ConsoleChannel::Outcome;
  if (command.size() > x2::retail::FrontEnd::kCommandMax) {
    control_reply_text(
        fd, 413, "Content Too Large",
        "\"%s\" is %zu characters; the console queue holds %zu\n",
        command.c_str(), command.size(), x2::retail::FrontEnd::kCommandMax);
    return;
  }
  switch (x2::control::channel().submit(command)) {
  case Outcome::Queued:
    control_reply_text(fd, 200, "OK",
                       "queued \"%s\"; the console runs it at its next safe "
                       "point and ignores a command it does not know\n",
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

void control_console_route(Socket fd, const char *query) {
  std::vector<char> command(std::strlen(query) + 1u);
  if (!control_query_arg(query, "command", command.data(), command.size()) ||
      !command[0]) {
    control_reply_text(fd, 400, "Bad Request",
                       "no command. Use /console?command=openmenu+online\n");
    return;
  }
  control_console_run(fd, command.data());
}

} // namespace x2::native
