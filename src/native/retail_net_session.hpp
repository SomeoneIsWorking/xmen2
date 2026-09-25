#ifndef X2_RETAIL_NET_SESSION_HPP
#define X2_RETAIL_NET_SESSION_HPP

/*
 * The retail network session, read the way its own menus read it.
 *
 * CNetPlayManager (FUN_006097b0) holds the online-menus flag in its first
 * byte (clear again once a game starts), the hosted save at +0x2250, and the
 * server browser's replies at +0x2208 as a list {head, -, cursor, count}: each
 * node is {-, key, next, prev, info}, and the browser handler (FUN_006090d0)
 * clears info+0x11 for a reply whose game version differs from ours -- the
 * list shows only the others. The session (FUN_00612be0, a static at
 * 0x00a53058) describes the hosted game in a 0x44-byte host-info block at
 * +0x3a4 (FUN_006156d0 builds it, version at +0x3e), and lists its players
 * at +0x26c as {head, -, cursor,
 * count}; each node is {-, -, next, -, player}, and a player is Ready while bit
 * 2 of +0x1c is set (FUN_00614240). The list is empty outside a network
 * session. FUN_006111f0 names this machine's player.
 */

extern "C" {
#include "x86rt.h"
}

#include <cstdint>

namespace x2::retail {

class NetSession {
public:
  explicit NetSession(const CPU &cpu);

  /* The CNetPlayManager singleton. */
  uint32_t manager() const { return manager_; }
  /* The online menus are up (Play Online through the lobby). */
  bool online() const;
  /* Browser replies the Games List shows: those of our game version. */
  uint32_t joinable_listed_games() const;

  uint32_t player_count() const;
  /* At least `minimum` players, every one of them Ready. */
  bool all_ready(uint32_t minimum) const;
  bool local_player_ready() const;

  /* The session's record of the game mode a hosted save runs in. */
  void set_game_mode(uint8_t mode) const;

  /* Rebuild the host-info block as the session's constructor leaves it --
     the state a first host starts from. Leaving a network game blanks its
     game version, and every browser then files the lobby as another
     version's and hides it. */
  void reset_host_info() const;

private:
  static bool ready(uint32_t player);

  const CPU &cpu_;
  uint32_t exe_;
  uint32_t manager_;
};

} // namespace x2::retail

#endif
