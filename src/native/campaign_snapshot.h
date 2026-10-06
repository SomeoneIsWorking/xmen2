#ifndef X2_CAMPAIGN_SNAPSHOT_H
#define X2_CAMPAIGN_SNAPSHOT_H

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

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "x86rt.h"

enum {
  X2_CAMPAIGN_SNAPSHOT_PAYLOAD_BYTES = 0x2fc00,
  X2_CAMPAIGN_SNAPSHOT_OBJECT_BYTES = 0x2fc78
};

typedef struct X2CampaignSnapshot X2CampaignSnapshot;

X2CampaignSnapshot *x2_campaign_snapshot_create(void);
/* 1 once the running campaign has been serialized into the snapshot. */
int x2_campaign_snapshot_capture(X2CampaignSnapshot *snapshot, const CPU *cpu);
/* The guest address of the save object, 0 before a capture. */
uint32_t x2_campaign_snapshot_address(const X2CampaignSnapshot *snapshot);

#ifdef __cplusplus
}
#endif

#endif
