#ifndef X2_RETAIL_HERO_TABLE_HPP
#define X2_RETAIL_HERO_TABLE_HPP

#include "guest_image_reader.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace x2::native {

/* One record of the game's character table as its menus show it; every field
   and its evidence is in docs/RE/menus.md. */
struct HeroRecord {
  /* record+0x150, the name scripts and menus key on ("sabretooth_hero"). */
  std::string name;
  /* record+0x170, the name the cards draw ("Sabretooth"), as UTF-8. */
  std::string display_name;
  /* What FUN_004b87c0 returns: record+0x1c plus the active mission's bonus. */
  int level = 0;
  /* The stats object's dead test (FUN_0044a690); false without stats. */
  bool fallen = false;
  /* The unlock bit the card tests (FUN_0048f770 on record+0x28e). */
  bool unlocked = false;
};

/* The character table (singleton 0x0071770c) read through the memory seam. */
class RetailHeroTable {
public:
  explicit RetailHeroTable(GuestImageReader &reader);

  /* Every named record, in table order. */
  bool read(std::vector<HeroRecord> *out);

private:
  bool read_record(std::uint32_t record, HeroRecord *out);
  bool read_mission_bonus(std::string *hero, int *bonus);
  bool read_unlocked(int id, bool *out);

  GuestImageReader &reader_;
};

/* The record the character lookup (FUN_004498d0) returns for `name`, which
   hashes the lowercased name; nullptr when none. */
const HeroRecord *find_hero(const std::vector<HeroRecord> &heroes,
                            std::string_view name);

} // namespace x2::native

#endif
