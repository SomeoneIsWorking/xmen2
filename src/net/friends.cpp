#include "friends.hpp"

#ifdef __EMSCRIPTEN__

namespace x2::net {

struct Friends::State {};

Friends::Friends() = default;
Friends::~Friends() = default;

FriendRoute *Friends::route() { return nullptr; }

void Friends::go_online(const std::filesystem::path &, const std::string &) {}

bool Friends::invite(std::string &error) {
  error = "a browser page cannot reach friends";
  return false;
}

bool Friends::paste(std::string_view, std::string &error) {
  error = "a browser page cannot reach friends";
  return false;
}

void Friends::poll() {}

std::string Friends::status() const {
  return "friends: none in a browser page";
}

} // namespace x2::net

#else

#include "friend_route.hpp"

#include <lucent/ipv4.h>
#include <lucent/log.h>
#include <lucent/p2p/connector.h>
#include <lucent/p2p/descriptor.h>
#include <lucent/p2p/virtual_lan.h>
#include <lucent/p2p/zerotier.h>

#include <mutex>

namespace x2::net {
namespace {

using lucent::p2p::CodePrefixes;
using lucent::p2p::CodeProblem;
using lucent::p2p::Connector;
using lucent::p2p::ConnectorConfig;

constexpr CodePrefixes kPrefixes{"XMI", "XMR"};
constexpr std::string_view kLabel = "xmen2-lan";
// Names the game's ad-hoc ZeroTier network and the UDP port on it.
constexpr std::uint16_t kZeroTierPort = 5166;

const char *step_name(Connector::Step step) {
  switch (step) {
  case Connector::Step::Gathering:
    return "gathering";
  case Connector::Step::Inviting:
    return "inviting";
  case Connector::Step::Trying:
    return "trying";
  case Connector::Step::Replying:
    return "replying";
  case Connector::Step::Punching:
    return "punching";
  case Connector::Step::Connected:
    return "connected";
  case Connector::Step::Failed:
    return "failed";
  }
  return "?";
}

const char *failure_name(Connector::Failure failure) {
  switch (failure) {
  case Connector::Failure::None:
    return "";
  case Connector::Failure::Unreachable:
    return "no way through to the other machine";
  case Connector::Failure::Lapsed:
    return "the invite ran out before anyone came";
  case Connector::Failure::NoNetwork:
    return "no network";
  }
  return "?";
}

const char *problem_name(CodeProblem problem) {
  switch (problem) {
  case CodeProblem::Empty:
    return "the code is empty";
  case CodeProblem::Unknown:
    return "not an X-Men Legends II code";
  case CodeProblem::WrongKind:
    return "an invite where a reply belongs, or a reply where an invite does";
  case CodeProblem::Damaged:
    return "a letter is wrong or missing";
  case CodeProblem::Version:
    return "the code is from another version";
  case CodeProblem::OtherInvite:
    return "a reply to another invite";
  }
  return "?";
}

ConnectorConfig config(const lucent::p2p::ZeroTierNode *node) {
  ConnectorConfig out;
  out.prefixes = kPrefixes;
  out.label = std::string{kLabel};
  out.zerotier = node;
  return out;
}

// The friends as the Winsock layer and the presence socket reach them.
class LanRoute final : public FriendRoute {
public:
  explicit LanRoute(lucent::p2p::VirtualLan &lan) : lan_(lan) {}

  bool is_friend(std::uint32_t address) const override {
    return lan_.is_friend(address);
  }
  bool send(const lucent::net::Endpoint &to, std::uint16_t from_port,
            std::string_view bytes) override {
    return lan_.send(to, from_port, bytes);
  }
  std::size_t broadcast(std::uint16_t to_port, std::uint16_t from_port,
                        std::string_view bytes) override {
    return lan_.broadcast(to_port, from_port, bytes);
  }
  std::optional<lucent::net::Endpoint>
  source_of(std::uint16_t loopback_port) const override {
    return lan_.source_of(loopback_port);
  }

private:
  lucent::p2p::VirtualLan &lan_;
};

} // namespace

struct Friends::State {
  lucent::p2p::VirtualLan lan;
  LanRoute route{lan};
  mutable std::mutex mutex;
  std::unique_ptr<lucent::p2p::ZeroTierNode> node;
  bool went_online = false;
  std::unique_ptr<Connector> meeting;
  bool hosting = false;
  std::string last;
  // How the last meeting ended, while no meeting runs.
  Friends::Stage ended = Friends::Stage::Idle;
  std::string problem;
};

Friends::Friends() : state_(std::make_unique<State>()) {}
Friends::~Friends() = default;

FriendRoute *Friends::route() { return &state_->route; }

void Friends::go_online(const std::filesystem::path &storage,
                        const std::string &roots) {
  const std::lock_guard lock(state_->mutex);
  if (state_->went_online) {
    return;
  }
  state_->went_online = true;
  std::string error;
  state_->node =
      lucent::p2p::ZeroTierNode::start({.storage = storage,
                                        .roots = roots,
                                        .port = 0,
                                        .network_port = kZeroTierPort},
                                       error);
  if (!state_->node) {
    lucent::warn("lan.friends", "no ZeroTier node: {}", error);
  }
}

bool Friends::invite(std::string &error) {
  const std::lock_guard lock(state_->mutex);
  if (state_->meeting) {
    error = "a meeting is already running";
    return false;
  }
  state_->meeting = std::make_unique<Connector>(config(state_->node.get()));
  state_->hosting = true;
  return true;
}

bool Friends::paste(std::string_view code, std::string &error) {
  const std::lock_guard lock(state_->mutex);
  if (state_->meeting && state_->hosting) {
    if (const std::optional<CodeProblem> problem =
            state_->meeting->reply(code)) {
      error = problem_name(*problem);
      return false;
    }
    return true;
  }
  if (state_->meeting) {
    error = "a meeting is already running";
    return false;
  }
  CodeProblem problem = CodeProblem::Empty;
  std::optional<lucent::p2p::Invite> invite =
      lucent::p2p::read_invite(code, kPrefixes, problem);
  if (!invite) {
    error = problem_name(problem);
    return false;
  }
  state_->meeting = std::make_unique<Connector>(config(state_->node.get()),
                                                std::move(*invite));
  state_->hosting = false;
  return true;
}

void Friends::poll() {
  const std::lock_guard lock(state_->mutex);
  if (!state_->meeting) {
    return;
  }
  const Connector::View view = state_->meeting->view();
  if (view.step == Connector::Step::Connected) {
    std::unique_ptr<lucent::p2p::Connection> connection =
        state_->meeting->take();
    const std::optional<std::uint32_t> address =
        state_->lan.add(std::move(connection));
    state_->last =
        address ? "connected: " + lucent::net::format_ipv4(*address)
                : std::string{"connected, but no room for another friend"};
    state_->ended = address ? Stage::Connected : Stage::Failed;
    state_->problem = address ? std::string{} : "no room for another friend";
    state_->meeting.reset();
  } else if (view.step == Connector::Step::Failed) {
    state_->last = std::string{"failed: "} + failure_name(view.failure);
    state_->ended = Stage::Failed;
    state_->problem = failure_name(view.failure);
    lucent::info("lan.friends", "the meeting failed: {}",
                 failure_name(view.failure));
    state_->meeting.reset();
  }
}

Friends::Shown Friends::shown() const {
  const std::lock_guard lock(state_->mutex);
  Shown out;
  out.friends = state_->lan.friends().size();
  if (!state_->meeting) {
    out.stage = state_->ended;
    out.problem = state_->problem;
    return out;
  }
  const Connector::View view = state_->meeting->view();
  out.hosting = state_->hosting;
  out.code = view.code.empty() ? std::string{} : lucent::p2p::spaced(view.code);
  switch (view.step) {
  case Connector::Step::Gathering:
    out.stage = Stage::Gathering;
    break;
  case Connector::Step::Inviting:
  case Connector::Step::Replying:
    out.stage = Stage::Waiting;
    break;
  case Connector::Step::Trying:
  case Connector::Step::Punching:
  case Connector::Step::Connected:
  case Connector::Step::Failed:
    out.stage = Stage::Connecting;
    break;
  }
  return out;
}

std::string Friends::status() const {
  const std::lock_guard lock(state_->mutex);
  std::string out = "friends:";
  for (const std::uint32_t address : state_->lan.friends()) {
    out += " " + lucent::net::format_ipv4(address);
  }
  if (state_->lan.friends().empty()) {
    out += " none";
  }
  out += state_->node && state_->node->reach() ? " | zerotier: on its network"
         : state_->node                        ? " | zerotier: joining"
                                               : " | zerotier: off";
  if (state_->meeting) {
    const Connector::View view = state_->meeting->view();
    out += std::string{" | meeting: "} +
           (state_->hosting ? "host " : "friend ") + step_name(view.step);
    if (!view.code.empty()) {
      out += " | code: " + lucent::p2p::spaced(view.code);
    }
    if (view.closed) {
      out += " | closed to a strict NAT";
    }
  } else if (!state_->last.empty()) {
    out += " | last meeting: " + state_->last;
  }
  return out;
}

} // namespace x2::net

#endif
