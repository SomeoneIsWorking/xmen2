#include "retail_hero_table.hpp"

namespace x2::native {
namespace {

/* FUN_0044b8f0's singleton; records at +4, 0x4f8 bytes each (FUN_004498d0). */
inline constexpr std::uint32_t kTablePointerRva = 0x0031770cu;
inline constexpr std::uint32_t kRecords = 0x4u;
inline constexpr std::uint32_t kRecordStride = 0x4f8u;
inline constexpr std::uint32_t kRecordCount = 31u;
inline constexpr std::uint32_t kRecordLevel = 0x1cu;
inline constexpr std::uint32_t kRecordStats = 0xc0u;
inline constexpr std::uint32_t kRecordName = 0x150u;
inline constexpr std::uint32_t kRecordDisplayName = 0x170u;
inline constexpr std::size_t kRecordNameBytes = 0x20u;
inline constexpr std::uint32_t kRecordUnlockId = 0x28eu;
/* The dead test FUN_0044a690: flag bit 0 set and health at or below 0. */
inline constexpr std::uint32_t kStatsHealth = 0x28u;
inline constexpr std::uint32_t kStatsFlags = 0x34u;
/* FUN_0048fed0's static object; FUN_0048f770 tests bit id of +0x1c0. */
inline constexpr std::uint32_t kUnlocksRva = 0x0032c530u;
inline constexpr std::uint32_t kUnlockBits = 0x1c0u;
inline constexpr int kUnlockIds = 0x129;
/* FUN_004cb560 and FUN_004cb590: the active mission indexes 0x78-byte
   records holding a hero name handle and that hero's level bonus. */
inline constexpr std::uint32_t kMissionIndexRva = 0x00382728u;
inline constexpr std::uint8_t kMissionNone = 0xfdu;
inline constexpr std::uint32_t kMissionHeroRva = 0x00384e48u;
inline constexpr std::uint32_t kMissionBonusRva = 0x00384e50u;
inline constexpr std::uint32_t kMissionStride = 0x78u;

bool same_name(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    const auto x = static_cast<unsigned char>(a[i]);
    const auto y = static_cast<unsigned char>(b[i]);
    const auto lx = x >= 'A' && x <= 'Z' ? x + 32u : x;
    const auto ly = y >= 'A' && y <= 'Z' ? y + 32u : y;
    if (lx != ly) {
      return false;
    }
  }
  return true;
}

} // namespace

RetailHeroTable::RetailHeroTable(GuestImageReader &reader) : reader_(reader) {}

bool RetailHeroTable::read_mission_bonus(std::string *hero, int *bonus) {
  hero->clear();
  *bonus = 0;
  std::uint8_t index = 0;
  if (!reader_.bytes(reader_.image(kMissionIndexRva), &index, 1u)) {
    return false;
  }
  if (index >= kMissionNone) {
    return true;
  }
  const std::uint32_t at = index * kMissionStride;
  std::uint32_t handle = 0;
  if (!reader_.u32(reader_.image(kMissionHeroRva + at), &handle)) {
    return false;
  }
  if (handle == 0u) {
    return true;
  }
  std::int16_t value = 0;
  if (!reader_.pool0_string(handle, hero) ||
      !reader_.bytes(reader_.image(kMissionBonusRva + at), &value,
                     sizeof value)) {
    return false;
  }
  *bonus = value;
  return true;
}

bool RetailHeroTable::read_unlocked(int id, bool *out) {
  *out = false;
  if (id < 0 || id >= kUnlockIds) {
    return true;
  }
  std::uint32_t word = 0;
  if (!reader_.u32(reader_.image(kUnlocksRva + kUnlockBits) +
                       static_cast<std::uint32_t>(id >> 5) * 4u,
                   &word)) {
    return false;
  }
  *out = (word & (1u << (static_cast<unsigned>(id) & 31u))) != 0u;
  return true;
}

bool RetailHeroTable::read_record(std::uint32_t record, HeroRecord *out) {
  std::string display;
  std::uint8_t level = 0;
  std::uint32_t stats = 0;
  std::int16_t unlock_id = 0;
  if (!reader_.c_string(record + kRecordName, kRecordNameBytes, &out->name) ||
      !reader_.c_string(record + kRecordDisplayName, kRecordNameBytes,
                        &display) ||
      !reader_.bytes(record + kRecordLevel, &level, 1u) ||
      !reader_.u32(record + kRecordStats, &stats) ||
      !reader_.bytes(record + kRecordUnlockId, &unlock_id, sizeof unlock_id) ||
      !read_unlocked(unlock_id, &out->unlocked)) {
    return false;
  }
  out->display_name = latin1_to_utf8(display);
  out->level = level;
  out->fallen = false;
  if (stats != 0u) {
    float health = 0.0F;
    std::uint8_t flags = 0;
    if (!reader_.bytes(stats + kStatsHealth, &health, sizeof health) ||
        !reader_.bytes(stats + kStatsFlags, &flags, 1u)) {
      return false;
    }
    out->fallen = (flags & 1u) != 0u && health <= 0.0F;
  }
  return true;
}

bool RetailHeroTable::read(std::vector<HeroRecord> *out) {
  out->clear();
  std::uint32_t table = 0;
  if (!reader_.u32(reader_.image(kTablePointerRva), &table)) {
    return false;
  }
  if (table == 0u) {
    return true;
  }
  std::string bonus_hero;
  int bonus = 0;
  if (!read_mission_bonus(&bonus_hero, &bonus)) {
    return false;
  }
  for (std::uint32_t n = 0; n < kRecordCount; ++n) {
    HeroRecord hero;
    if (!read_record(table + kRecords + n * kRecordStride, &hero)) {
      return false;
    }
    if (hero.name.empty()) {
      continue;
    }
    if (!bonus_hero.empty() && same_name(hero.name, bonus_hero)) {
      hero.level = static_cast<std::int16_t>(hero.level + bonus);
    }
    out->push_back(std::move(hero));
  }
  return true;
}

const HeroRecord *find_hero(const std::vector<HeroRecord> &heroes,
                            std::string_view name) {
  for (const HeroRecord &hero : heroes) {
    if (same_name(hero.name, name)) {
      return &hero;
    }
  }
  return nullptr;
}

} // namespace x2::native
