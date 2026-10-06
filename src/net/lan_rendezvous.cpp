#include "lan_rendezvous.hpp"

namespace x2::net::presence {

void RendezvousTable::begin(uint32_t cookie, uint8_t index,
                            const Endpoint &self, double now) {
  Pairing pairing;
  pairing.index = index;
  pairing.self = self;
  pairing.deadline = now + kPartnerSeconds;
  pairing.next_send = now;
  pairings_[cookie] = pairing;
}

void RendezvousTable::cancel(uint32_t cookie) { pairings_.erase(cookie); }

void RendezvousTable::heard(const Rendezvous &message) {
  const auto found = pairings_.find(message.cookie);
  if (found == pairings_.end() || found->second.index == message.index) {
    return;
  }
  Pairing &pairing = found->second;
  const Endpoint partner{message.address, message.port};
  if (!pairing.partner) {
    pairing.partner = partner;
    /* Tell the partner at once rather than at the next resend. */
    pairing.next_send = 0.0;
  }
  pairing.partner_heard_us |= message.partner_heard;
}

std::vector<Rendezvous> RendezvousTable::due(double now) {
  std::vector<Rendezvous> messages;
  for (auto &[cookie, pairing] : pairings_) {
    if ((pairing.partner_heard_us && pairing.told_partner) ||
        now < pairing.next_send) {
      continue;
    }
    pairing.next_send = now + kResendSeconds;
    pairing.told_partner |= pairing.partner.has_value();
    messages.push_back({cookie, pairing.index, pairing.self.address,
                        pairing.self.port, pairing.partner.has_value()});
  }
  return messages;
}

std::vector<RendezvousTable::Outcome>
RendezvousTable::take_outcomes(double now) {
  std::vector<Outcome> outcomes;
  for (auto at = pairings_.begin(); at != pairings_.end();) {
    Pairing &pairing = at->second;
    if (pairing.partner && !pairing.reported) {
      pairing.reported = true;
      outcomes.push_back({at->first, pairing.partner});
    }
    const bool expired = now >= pairing.deadline;
    if (!pairing.partner && expired) {
      outcomes.push_back({at->first, std::nullopt});
    }
    if (expired || (pairing.reported && pairing.partner_heard_us &&
                    pairing.told_partner)) {
      at = pairings_.erase(at);
    } else {
      ++at;
    }
  }
  return outcomes;
}

} // namespace x2::net::presence
