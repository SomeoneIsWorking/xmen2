#ifndef X2_EXTRACTION_REVIVE_POLICY_HPP
#define X2_EXTRACTION_REVIVE_POLICY_HPP

/* The retail revive cost and whether the party can pay it. */

#include <span>
#include <string>

namespace x2::extraction {

/* The cheapest revive the game sells (0x004b8830). */
inline constexpr int kReviveCostFloor = 200;

/* One fallen hero's retail revive cost: max(200, 2 * level^2) (0x004b8830). */
int revive_cost(int level);

struct RevivePlan {
  enum class Outcome { NothingFallen, Insufficient, Affordable };

  Outcome outcome = Outcome::NothingFallen;
  int fallen = 0;
  int total = 0;
  int money = 0;
};

/* The sum over every fallen hero, judged against the party's money. */
RevivePlan plan_revive(std::span<const int> fallen_levels, int money);

/* The line a player reads: the offer, or why it cannot be taken. */
std::string offer_text(const RevivePlan &plan);

} // namespace x2::extraction

#endif
