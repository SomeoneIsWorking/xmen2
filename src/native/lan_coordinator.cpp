#include "lan_coordinator.hpp"

#include "guest_call.hpp"
#include "lan_session_director.hpp"
#include "retail_front_end.hpp"
#include "retail_net_session.hpp"

extern "C" {
#include "continue_runtime.h"
#include "x2_log.h"
}

#include <random>
#include <unistd.h>

namespace x2::lan {
namespace {

namespace presence = net::presence;

/* The map path FUN_00484ce0 compares ("act2/savage/savage1"). */
inline constexpr uint32_t kMapPath = 0x1e0u;
inline constexpr size_t kMapPathLimit = 128u;
/* The retail session seats four. */
inline constexpr uint8_t kMaxPlayers = 4u;
/* A request is a datagram; repeat it until the host's lobby is heard. */
inline constexpr double kRequestRepeatSeconds = 2.0;
/* A re-form leaves a level and walks four menus; far longer means it failed
   on the host, and the host's own log names why. */
inline constexpr double kJoinGiveUpSeconds = 90.0;
inline constexpr size_t kHostNameBytes = 64u;

uint64_t random_instance() {
  std::random_device entropy;
  return (static_cast<uint64_t>(entropy()) << 32u) | entropy();
}

std::string machine_name() {
  std::string name(kHostNameBytes, '\0');
  if (gethostname(name.data(), name.size()) != 0) {
    return "X-Men Legends II";
  }
  name.resize(name.find('\0') == std::string::npos ? name.size()
                                                   : name.find('\0'));
  return name.empty() ? "X-Men Legends II" : name;
}

const char *phase_name(presence::Phase phase) {
  switch (phase) {
  case presence::Phase::Playing:
    return "playing";
  case presence::Phase::Lobby:
    return "lobby";
  case presence::Phase::NetworkGame:
    return "network game";
  }
  return "unknown";
}

} // namespace

Coordinator::Coordinator()
    : presence_(random_instance()), name_(machine_name()) {}

void Coordinator::map_loaded(uint32_t map, bool succeeded) {
  if (succeeded && map) {
    map_ = std::string(guest::guest_string(map + kMapPath, kMapPathLimit));
  }
}

void Coordinator::start_presence() {
  started_ = true;
  if (!presence_.start()) {
    x2_log_info("lan: presence is off: %s", presence_.error().c_str());
    set_status("presence off: " + presence_.error());
    return;
  }
  x2_log_info("lan: presence on UDP %u as \"%s\"", presence::kPort,
              name_.c_str());
}

presence::LocalFacts Coordinator::read_facts(const CPU &cpu) {
  const retail::NetSession session(cpu);
  const SessionDirector &director = session_director();
  presence::LocalFacts facts;
  facts.map = map_;
  facts.hosting_lobby = director.hosting_lobby();
  facts.session_players = static_cast<uint8_t>(session.player_count());
  if (director.menu() == "host") {
    lobby_role_ = LobbyRole::Host;
  } else if (director.menu() == "join") {
    lobby_role_ = LobbyRole::Client;
  }
  facts.client = lobby_role_ == LobbyRole::Client;
  return facts;
}

void Coordinator::follow_lost_host(const presence::LocalFacts &facts,
                                   double now) {
  if (facts.session_players != 0u) {
    return;
  }
  /* The session emptied. Still in the level as a client: the host left
     without us (a re-form, or a crash); anywhere else the player left. */
  const bool lost = lobby_role_ == LobbyRole::Client &&
                    presence::is_campaign_map(facts.map) && joined_ &&
                    !pending_ && !session_director().directing();
  lobby_role_ = LobbyRole::None;
  if (!lost) {
    return;
  }
  pending_ = joined_;
  pending_asks_ = false;
  pending_since_ = now;
  x2_log_info("lan: lost \"%s\" mid-game; rejoining when its lobby opens",
              pending_->name.c_str());
}

void Coordinator::report_socket_error() {
  if (presence_.error() != reported_error_) {
    reported_error_ = presence_.error();
    if (!reported_error_.empty()) {
      x2_log_error("lan: presence socket: %s", reported_error_.c_str());
    }
  }
}

void Coordinator::answer_join_requests(
    const std::vector<uint64_t> &requesters, const presence::LocalFacts &facts,
    const std::optional<presence::Announce> &local) {
  if (requesters.empty()) {
    return;
  }
  SessionDirector &director = session_director();
  /* A re-form under way, or its open lobby: a machine that asks now is one
     more player to wait for, however many asked in the same poll. */
  if (director.hosting()) {
    const size_t known = requesters_.size();
    requesters_.insert(requesters.begin(), requesters.end());
    if (requesters_.size() != known) {
      const auto expected =
          static_cast<uint32_t>(reform_present_ + requesters_.size());
      director.expect_players(expected);
      x2_log_info("lan: %zu more player(s) asked to join; the lobby now "
                  "waits for %u",
                  requesters_.size() - known, expected);
    }
    return;
  }
  if (!local) {
    x2_log_info("lan: a join request arrived while this game is not "
                "joinable; ignored");
    return;
  }
  /* Everyone in the game now comes back through the lobby, and so does each
     machine that asked. */
  const uint32_t present =
      local->phase == presence::Phase::NetworkGame ? facts.session_players : 1u;
  const std::set<uint64_t> asked(requesters.begin(), requesters.end());
  const auto expected = static_cast<uint32_t>(present + asked.size());
  if (director.request(SessionDirector::Role::Host, expected)) {
    reform_present_ = present;
    requesters_ = asked;
    x2_log_info("lan: %zu player(s) asked to join; re-forming this %s as a "
                "LAN lobby for %u",
                asked.size(), phase_name(local->phase), expected);
  }
}

const presence::Peer *Coordinator::joinable_peer() const {
  for (const presence::Peer &peer : presence_.peers().peers()) {
    if (peer.announce.players < kMaxPlayers) {
      return &peer;
    }
  }
  return nullptr;
}

void Coordinator::join_chosen() {
  const presence::Peer *peer = joinable_peer();
  if (!peer) {
    x2_log_info("lan: Join chosen, but no LAN game is announced any more");
    return;
  }
  pending_ = Host{peer->instance, peer->announce.name};
  pending_asks_ = true;
  pending_since_ = now_;
  next_request_ = now_;
  x2_log_info("lan: joining \"%s\" (%s)", pending_->name.c_str(),
              phase_name(peer->announce.phase));
}

void Coordinator::follow_pending_join(double now) {
  if (!pending_) {
    return;
  }
  const presence::Peer *host = presence_.peers().find(pending_->instance);
  if (host && host->announce.phase == presence::Phase::Lobby) {
    if (session_director().request(SessionDirector::Role::Join)) {
      x2_log_info("lan: \"%s\" has its lobby open; joining",
                  pending_->name.c_str());
      joined_ = pending_;
      pending_.reset();
    }
    return;
  }
  if (now - pending_since_ > kJoinGiveUpSeconds) {
    x2_log_error("lan: \"%s\" did not open its lobby within %.0f s; the "
                 "join is abandoned",
                 pending_->name.c_str(), kJoinGiveUpSeconds);
    pending_.reset();
    return;
  }
  if (pending_asks_ && now >= next_request_) {
    next_request_ = now + kRequestRepeatSeconds;
    presence_.request_join(pending_->instance);
  }
}

void Coordinator::refresh_label(const CPU &cpu) {
  if (pending_) {
    label_ = "Joining " + pending_->name + "...";
  } else if (const presence::Peer *peer = joinable_peer()) {
    label_ = "Join " + peer->announce.name;
  } else {
    label_.clear();
  }
  if (label_ == shown_label_) {
    return;
  }
  if (retail::FrontEnd::active_menu(cpu) != "main") {
    return;
  }
  shown_label_ = label_;
  CPU call = cpu;
  x2_main_menu_refresh(&call, retail::FrontEnd::active_menu_object(cpu));
}

const char *Coordinator::join_label() const {
  return label_.empty() ? nullptr : label_.c_str();
}

void Coordinator::poll(const CPU &cpu, double now) {
  now_ = now;
  if (!started_) {
    start_presence();
  }
  session_director().poll(cpu, now);
  presence::LocalFacts facts = read_facts(cpu);
  follow_lost_host(facts, now);
  facts.directing = session_director().directing() || pending_.has_value();
  const auto local = presence::local_announce(facts, name_);
  const auto heard = presence_.poll(now, local);
  report_socket_error();
  nat_.poll(cpu, now, heard.rendezvous, presence_);
  answer_join_requests(heard.join_requesters, facts, local);
  follow_pending_join(now);
  refresh_label(cpu);
  set_status(describe(facts, local));
}

std::string
Coordinator::describe(const presence::LocalFacts &facts,
                      const std::optional<presence::Announce> &local) const {
  std::string text = "presence: ";
  text += local ? phase_name(local->phase) : "not announced";
  text += "; " + std::to_string(presence_.peers().peers().size()) +
          " LAN game(s) heard";
  if (!label_.empty()) {
    text += "; row \"" + label_ + "\"";
  }
  text += "\nfacts: map \"" + facts.map + "\", client " +
          std::to_string(facts.client) + ", session players " +
          std::to_string(facts.session_players);
  return text;
}

void Coordinator::set_status(std::string text) {
  std::lock_guard lock(status_mutex_);
  status_ = std::move(text);
}

std::string Coordinator::status() const {
  std::lock_guard lock(status_mutex_);
  return status_;
}

namespace {
Coordinator g_coordinator;
} // namespace

Coordinator &coordinator() { return g_coordinator; }

} // namespace x2::lan
