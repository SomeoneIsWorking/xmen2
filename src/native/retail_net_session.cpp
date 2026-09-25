#include "retail_net_session.hpp"

#include "guest_call.hpp"

namespace x2::retail {
namespace {

inline constexpr uint32_t kNetManagerRva = 0x002097b0u;
inline constexpr uint32_t kBrowserReplies = 0x2208u;
inline constexpr uint32_t kReplyInfo = 0x10u;
inline constexpr uint32_t kInfoCompatible = 0x11u;
inline constexpr uint32_t kSessionHostInfo = 0x3a4u;
inline constexpr uint32_t kHostInfoConstructRva = 0x002156d0u;
inline constexpr uint32_t kSessionRva = 0x00653058u;
inline constexpr uint32_t kSessionGameMode = 0x3dfu;
inline constexpr uint32_t kSessionPlayers = 0x26cu;
inline constexpr uint32_t kListCount = 0xcu;
inline constexpr uint32_t kNodeNext = 0x8u;
inline constexpr uint32_t kNodePlayer = 0x10u;
inline constexpr uint32_t kPlayerFlags = 0x1cu;
inline constexpr uint32_t kPlayerReady = 0x4u;
inline constexpr uint32_t kLocalPlayerRva = 0x002111f0u;

} // namespace

NetSession::NetSession(const CPU &cpu)
    : cpu_(cpu), exe_(x86_module_base("XMen2.exe")),
      manager_(guest::GuestCall(cpu).cdecl_call(exe_ + kNetManagerRva)) {}

bool NetSession::online() const { return manager_ && RD8(manager_) != 0u; }

uint32_t NetSession::joinable_listed_games() const {
  uint32_t joinable = 0u;
  if (!manager_) {
    return joinable;
  }
  for (uint32_t node = RD32(manager_ + kBrowserReplies); node;
       node = RD32(node + kNodeNext)) {
    const uint32_t info = RD32(node + kReplyInfo);
    joinable += info && RD8(info + kInfoCompatible) != 0u;
  }
  return joinable;
}

uint32_t NetSession::player_count() const {
  return RD32(exe_ + kSessionRva + kSessionPlayers + kListCount);
}

bool NetSession::ready(uint32_t player) {
  return player && (RD32(player + kPlayerFlags) & kPlayerReady) != 0u;
}

bool NetSession::all_ready(uint32_t minimum) const {
  if (player_count() < minimum) {
    return false;
  }
  const uint32_t players = exe_ + kSessionRva + kSessionPlayers;
  for (uint32_t node = RD32(players); node; node = RD32(node + kNodeNext)) {
    if (!ready(RD32(node + kNodePlayer))) {
      return false;
    }
  }
  return true;
}

bool NetSession::local_player_ready() const {
  return ready(guest::GuestCall(cpu_).thiscall(exe_ + kLocalPlayerRva,
                                               exe_ + kSessionRva));
}

void NetSession::set_game_mode(uint8_t mode) const {
  WR8(exe_ + kSessionRva + kSessionGameMode, mode);
}

void NetSession::reset_host_info() const {
  guest::GuestCall(cpu_).thiscall(exe_ + kHostInfoConstructRva,
                                  exe_ + kSessionRva + kSessionHostInfo);
}

} // namespace x2::retail
