#pragma once

/*
 * The running campaign, serialized the way a save file holds it.
 *
 * The game owner's slot 0x208 writes the live campaign into a 0x2fc78-byte
 * save object: a 0x2fc00-byte payload, the stream cursor at +0x2fc00 and two
 * header flags. That object is what a save file stores, what the load menu
 * applies, and what the net manager hosts and streams to joiners
 * (FUN_00608260), so both the autosave and the LAN session capture through
 * this one owner (x2::save::CampaignSnapshot).
 *
 * A finished capture is rewound, as a save read from disk is: applying it
 * (owner slot 0x20c) reads from the cursor, and a cursor left at the end of
 * the serialized campaign reads the zeroed tail instead -- difficulty 0
 * among the rest.
 */

#include <cstdint>

#include "x86rt.h"

namespace x2::native {

inline constexpr int kCampaignSnapshotPayloadBytes = 0x2fc00;
inline constexpr int kCampaignSnapshotObjectBytes = 0x2fc78;

struct CampaignSnapshot;

CampaignSnapshot *campaign_snapshot_create();
/* 1 once the running campaign has been serialized into the snapshot. */
int campaign_snapshot_capture(CampaignSnapshot *snapshot, const CPU *cpu);
/* The guest address of the save object, 0 before a capture. */
std::uint32_t campaign_snapshot_address(const CampaignSnapshot *snapshot);

} // namespace x2::native
