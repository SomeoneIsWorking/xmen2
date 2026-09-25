#ifndef X2_CAMPAIGN_SNAPSHOT_HPP
#define X2_CAMPAIGN_SNAPSHOT_HPP

#include "campaign_snapshot.h"

#include <cstdint>

namespace x2::save {

/* One guest save object, reused across captures and freed with its owner. */
class CampaignSnapshot {
public:
  CampaignSnapshot() = default;
  ~CampaignSnapshot();
  CampaignSnapshot(const CampaignSnapshot &) = delete;
  CampaignSnapshot &operator=(const CampaignSnapshot &) = delete;

  bool capture(const CPU &cpu);
  uint32_t address() const { return captured_ ? object_ : 0u; }

private:
  uint32_t object_ = 0;
  bool captured_ = false;
};

} // namespace x2::save

#endif
