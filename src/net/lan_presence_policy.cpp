#include "lan_presence_policy.hpp"

#include <algorithm>
#include <array>
#include <cctype>

namespace x2::net::presence {
namespace {

inline constexpr std::array<std::string_view, 2> kCampaignFolders = {
    "act", "briefing/"};

bool starts_with_folded(std::string_view text, std::string_view prefix) {
  return text.size() >= prefix.size() &&
         std::equal(prefix.begin(), prefix.end(), text.begin(),
                    [](char expected, char actual) {
                      return expected ==
                             std::tolower(static_cast<unsigned char>(actual));
                    });
}

} // namespace

bool is_campaign_map(std::string_view map) {
  return std::any_of(kCampaignFolders.begin(), kCampaignFolders.end(),
                     [map](std::string_view folder) {
                       return starts_with_folded(map, folder);
                     });
}

std::optional<Announce> local_announce(const LocalFacts &facts,
                                       const std::string &name) {
  if (facts.hosting_lobby) {
    return Announce{Phase::Lobby, facts.session_players, name};
  }
  if (facts.directing || !is_campaign_map(facts.map)) {
    return std::nullopt;
  }
  if (facts.session_players == 0u) {
    return Announce{Phase::Playing, 1u, name};
  }
  if (facts.client) {
    return std::nullopt;
  }
  return Announce{Phase::NetworkGame, facts.session_players, name};
}

bool should_follow_host(const SessionEnd &end) {
  return end.client && end.knows_host && !end.busy && !end.left_by_choice &&
         is_campaign_map(end.map);
}

bool PeerTable::heard(uint64_t instance, const Announce &announce, double now) {
  auto known =
      std::find_if(peers_.begin(), peers_.end(), [instance](const Peer &peer) {
        return peer.instance == instance;
      });
  if (known == peers_.end()) {
    peers_.push_back({instance, announce, now});
    return true;
  }
  known->heard = now;
  if (known->announce == announce) {
    return false;
  }
  known->announce = announce;
  return true;
}

bool PeerTable::expire(double now) {
  const auto gone =
      std::remove_if(peers_.begin(), peers_.end(), [now](const Peer &peer) {
        return now - peer.heard > kExpirySeconds;
      });
  const bool changed = gone != peers_.end();
  peers_.erase(gone, peers_.end());
  return changed;
}

const Peer *PeerTable::find(uint64_t instance) const {
  const auto known =
      std::find_if(peers_.begin(), peers_.end(), [instance](const Peer &peer) {
        return peer.instance == instance;
      });
  return known == peers_.end() ? nullptr : &*known;
}

} // namespace x2::net::presence
