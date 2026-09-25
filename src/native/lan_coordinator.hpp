#ifndef X2_LAN_COORDINATOR_HPP
#define X2_LAN_COORDINATOR_HPP

/*
 * LAN play without a lobby to find (issue #188).
 *
 * A game being played is announced on the LAN. Another machine's main menu
 * shows the first one it hears as a "Join <host>" row; choosing it asks
 * that host to open its lobby, and once the host announces the lobby the
 * joiner walks in. Both halves are the SessionDirector's scripts through the
 * retail menus; the coordinator only decides when each runs.
 *
 *   host   Playing  --JoinRequest-->  re-form (director Host)  -->  Lobby
 *   joiner  Join row  -->  JoinRequest, repeated  -->  host in Lobby  -->
 *          director Join
 *
 * A re-form ends the host's network game, and every client already in it
 * loses its host while still in the level. Those clients wait, unannounced,
 * for the same host's lobby and walk back in, so a drop-in costs the others
 * one load and nothing else. A player who quits through the pause menu's
 * Quit Game also empties its session mid-level; that is told apart by the
 * script the quit dialog runs, and such a player stays at the main menu.
 */

#include "lan_nat_negotiation.hpp"
#include "lan_presence.hpp"

extern "C" {
#include "x86rt.h"
}

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace x2::lan {

class Coordinator {
public:
  Coordinator();

  /* The guest's thread: FUN_00484ce0 finished loading `map`. */
  void map_loaded(uint32_t map, bool succeeded);

  /* The guest's input thread, once per poll. */
  void poll(const CPU &cpu, double now);

  /* The guest's thread: the main menu's Join row text, or nullptr. */
  const char *join_label() const;

  /* The guest's thread: the player chose the Join row. */
  void join_chosen();

  /* The guest's thread: mainMenuExit() ran. While a session is still up
     that is the player choosing to leave (the quit dialog's Yes), and the
     session emptying after it is not followed. */
  void left_by_choice(const CPU &cpu);

  /* Any thread. */
  std::string status() const;

  /* The guest thread: GameSpy's NAT negotiation, answered on the LAN. */
  NatNegotiation &nat_negotiation() { return nat_; }

private:
  void start_presence();
  net::presence::LocalFacts read_facts(const CPU &cpu);
  void
  answer_join_requests(const std::vector<uint64_t> &requesters,
                       const net::presence::LocalFacts &facts,
                       const std::optional<net::presence::Announce> &local);
  void follow_pending_join(double now);
  void follow_lost_host(const net::presence::LocalFacts &facts, double now);
  const net::presence::Peer *joinable_peer() const;
  void refresh_label(const CPU &cpu);
  void report_socket_error();
  std::string
  describe(const net::presence::LocalFacts &facts,
           const std::optional<net::presence::Announce> &local) const;
  void set_status(std::string text);

  net::presence::Presence presence_;
  NatNegotiation nat_;
  std::string name_;
  std::string map_;
  /* Which lobby menu this machine last passed through: only a host sees
     "host", only a joiner "join". Forgotten when the session empties. */
  enum class LobbyRole { None, Host, Client };
  LobbyRole lobby_role_ = LobbyRole::None;
  bool started_ = false;
  double now_ = 0.0;

  /* A host this machine is joining or playing with. */
  struct Host {
    uint64_t instance = 0u;
    std::string name;
  };
  /* Waiting for `pending_` to open its lobby: chosen from the menu (asking
     it, repeatedly), or lost mid-game while it re-forms (only waiting). */
  std::optional<Host> pending_;
  bool pending_asks_ = false;
  double pending_since_ = 0.0;
  double next_request_ = 0.0;
  /* Who asked this host to open the lobby it is re-forming or holding. */
  std::set<uint64_t> requesters_;
  /* The players in the game when that re-form began. */
  uint32_t reform_present_ = 1u;
  /* The host of the network game this machine last joined. */
  std::optional<Host> joined_;
  /* The player chose to leave; consumed when the session empties. */
  bool left_by_choice_ = false;

  std::string label_;
  std::string shown_label_;
  std::string reported_error_;

  mutable std::mutex status_mutex_;
  std::string status_ = "presence not started";
};

Coordinator &coordinator();

} // namespace x2::lan

#endif
