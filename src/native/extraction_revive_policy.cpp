#include "extraction_revive_policy.hpp"

#include <algorithm>

namespace x2::extraction {

int revive_cost(int level) {
  return std::max(kReviveCostFloor, 2 * level * level);
}

RevivePlan plan_revive(std::span<const int> fallen_levels, int money) {
  RevivePlan plan;
  plan.money = money;
  plan.fallen = static_cast<int>(fallen_levels.size());
  for (const int level : fallen_levels) {
    plan.total += revive_cost(level);
  }
  if (plan.fallen == 0) {
    plan.outcome = RevivePlan::Outcome::NothingFallen;
  } else if (plan.total > money) {
    plan.outcome = RevivePlan::Outcome::Insufficient;
  } else {
    plan.outcome = RevivePlan::Outcome::Affordable;
  }
  return plan;
}

std::string offer_text(const RevivePlan &plan) {
  if (plan.outcome == RevivePlan::Outcome::NothingFallen) {
    return {};
  }
  std::string text =
      "Revive " + std::to_string(plan.fallen) +
      (plan.fallen == 1 ? " fallen hero: " : " fallen heroes: ") +
      std::to_string(plan.total);
  if (plan.outcome == RevivePlan::Outcome::Insufficient) {
    text += " (not enough money, you have " + std::to_string(plan.money) + ")";
  }
  return text;
}

} // namespace x2::extraction
