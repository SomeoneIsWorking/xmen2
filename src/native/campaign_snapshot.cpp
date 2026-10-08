#include "campaign_snapshot.hpp"

#include "guest_call.hpp"

namespace x2::save {
namespace {

/* FUN_0046dce0: the game owner. Slot 0x208 serializes the campaign into the
   save object passed to it. */
inline constexpr uint32_t kGameOwnerRva = 0x0006dce0u;
inline constexpr uint32_t kSerializerSlot = 0x208u;
/* The stream cursor: the guest address the next read or write uses. */
inline constexpr uint32_t kCursor = 0x2fc00u;
inline constexpr uint32_t kHeaderFlagA = 0x2fc04u;
inline constexpr uint32_t kHeaderFlagB = 0x2fc44u;

} // namespace

CampaignSnapshot::~CampaignSnapshot() {
  if (object_) {
    guest_free(object_);
  }
}

bool CampaignSnapshot::capture(const CPU &cpu) {
  captured_ = false;
  const uint32_t exe = x86_module_base("XMen2.exe");
  if (!exe) {
    return false;
  }
  if (!object_) {
    object_ = guest_malloc(native::kCampaignSnapshotObjectBytes);
  }
  if (!object_) {
    return false;
  }
  std::memset(guest_memory_pointer(object_), 0,
              native::kCampaignSnapshotObjectBytes);
  WR32(object_ + kCursor, object_);
  WR8(object_ + kHeaderFlagA, 0u);
  WR8(object_ + kHeaderFlagB, 0u);

  const uint32_t owner = guest::GuestCall(cpu).cdecl_call(exe + kGameOwnerRva);
  if (!owner || !RD32(owner) || !RD32(RD32(owner) + kSerializerSlot)) {
    return false;
  }
  guest::GuestCall(cpu).virtual_call(owner, kSerializerSlot, {object_});
  WR32(object_ + kCursor, object_);
  captured_ = true;
  return true;
}

} // namespace x2::save

namespace x2::native {

struct CampaignSnapshot {
  save::CampaignSnapshot snapshot;
};

CampaignSnapshot *campaign_snapshot_create() { return new CampaignSnapshot(); }

int campaign_snapshot_capture(CampaignSnapshot *snapshot, const CPU *cpu) {
  return snapshot && cpu && snapshot->snapshot.capture(*cpu);
}

std::uint32_t campaign_snapshot_address(const CampaignSnapshot *snapshot) {
  return snapshot ? snapshot->snapshot.address() : 0u;
}

} // namespace x2::native
