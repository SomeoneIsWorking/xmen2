/*
 * The extraction revive rules and the prompt that carries a request, through
 * the shipping policy, prompt and entity reader (docs/RE/extraction.md).
 */
#include "extraction_revive_policy.hpp"
#include "retail_entities.hpp"
#include "revive_prompt.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures;
int checks;

void check(bool ok, const std::string &what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what.c_str());
  }
}

using x2::extraction::RevivePlan;

void cost_and_plan() {
  using namespace x2::extraction;
  check(revive_cost(1) == 200, "level 1 costs the floor");
  check(revive_cost(10) == 200, "level 10 (2*100) is still the floor");
  check(revive_cost(11) == 242, "level 11 costs 2*121");
  check(revive_cost(30) == 1800, "level 30 costs 2*900");

  const std::array<int, 3> levels{1, 11, 30};
  const RevivePlan sum = plan_revive(levels, 5000);
  check(sum.fallen == 3 && sum.total == 200 + 242 + 1800,
        "three fallen heroes sum their own costs");
  check(sum.outcome == RevivePlan::Outcome::Affordable, "5000 pays 2242");

  const RevivePlan exact = plan_revive(levels, 2242);
  check(exact.outcome == RevivePlan::Outcome::Affordable,
        "money equal to the total pays it");
  const RevivePlan short_by_one = plan_revive(levels, 2241);
  check(short_by_one.outcome == RevivePlan::Outcome::Insufficient,
        "one short of the total is refused");
  check(short_by_one.money == 2241, "the plan keeps the money it judged");

  const RevivePlan none = plan_revive(std::span<const int>{}, 1000);
  check(none.outcome == RevivePlan::Outcome::NothingFallen && none.total == 0 &&
            offer_text(none).empty(),
        "nobody fallen offers nothing");

  const std::array<int, 1> one{1};
  check(offer_text(plan_revive(one, 200)) == "Revive 1 fallen hero: 200",
        "a single affordable offer reads with its total");
  check(offer_text(plan_revive(levels, 5000)) == "Revive 3 fallen heroes: 2242",
        "several heroes pluralise and show the summed total");
  check(offer_text(plan_revive(one, 199)) ==
            "Revive 1 fallen hero: 200 (not enough money, you have 199)",
        "an unaffordable offer says why");
}

SDL_Event key_down(SDL_Keycode key, bool repeat = false) {
  SDL_Event event{};
  event.type = SDL_EVENT_KEY_DOWN;
  event.key.key = key;
  event.key.repeat = repeat;
  return event;
}

SDL_Event pad_down(SDL_GamepadButton button) {
  SDL_Event event{};
  event.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
  event.gbutton.button = static_cast<Uint8>(button);
  return event;
}

SDL_Event finger(Uint32 type, long long id, float x, float y) {
  SDL_Event event{};
  event.type = type;
  event.tfinger.fingerID = static_cast<SDL_FingerID>(id);
  event.tfinger.x = x;
  event.tfinger.y = y;
  return event;
}

void prompt_requests() {
  using x2::input::RevivePrompt;
  RevivePrompt prompt;

  check(!prompt.request(), "a request with nothing offered is refused");
  check(!prompt.handle_event(key_down(SDLK_F3)),
        "F3 with nothing offered is left to the game");
  check(!prompt.take_request(), "no request was kept");

  prompt.offer("Revive 1 fallen hero: 200", true);
  check(prompt.view().offered &&
            prompt.view().text.find("200") != std::string::npos,
        "the offer is visible with its text");
  check(!prompt.handle_event(key_down(SDLK_F3, true)),
        "a held F3 does not re-request");
  check(prompt.handle_event(key_down(SDLK_F3)), "F3 takes the offer");
  check(prompt.take_request(), "the poll takes the request");
  check(!prompt.take_request(), "a request is taken once");

  check(prompt.handle_event(pad_down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)),
        "the controller's left shoulder takes the offer");
  check(prompt.take_request(), "the shoulder's request is kept");
  check(!prompt.handle_event(pad_down(SDL_GAMEPAD_BUTTON_BACK)),
        "Back is not the prompt's");

  const auto &rect = x2::input::kRevivePromptRect;
  const float cx = (rect.left + rect.right) / 2.0F;
  const float cy = (rect.top + rect.bottom) / 2.0F;
  check(!prompt.handle_event(finger(SDL_EVENT_FINGER_DOWN, 7, 0.1F, 0.9F)),
        "a finger outside the prompt is not taken");
  check(prompt.handle_event(finger(SDL_EVENT_FINGER_DOWN, 7, cx, cy)),
        "a finger on the prompt takes the offer");
  check(prompt.handle_event(finger(SDL_EVENT_FINGER_MOTION, 7, 0.1F, 0.9F)),
        "the touch runtime does not see that finger move");
  check(!prompt.handle_event(finger(SDL_EVENT_FINGER_UP, 8, cx, cy)),
        "another finger's lift is not the prompt's");
  check(prompt.handle_event(finger(SDL_EVENT_FINGER_UP, 7, cx, cy)),
        "the prompt's finger lift is the prompt's");
  check(!prompt.handle_event(finger(SDL_EVENT_FINGER_UP, 7, cx, cy)),
        "the finger is released after its lift");
  check(prompt.take_request(), "the tap's request is kept");

  prompt.withdraw();
  check(!prompt.view().offered, "a withdrawn offer is gone");
  check(!prompt.handle_event(finger(SDL_EVENT_FINGER_DOWN, 9, cx, cy)),
        "a finger on the vanished prompt reaches the touch controls");
}

void prompt_notice() {
  x2::input::RevivePrompt prompt(0.05);
  prompt.announce("Not enough money");
  check(prompt.view().notice == "Not enough money", "a fresh notice shows");
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  check(prompt.view().notice.empty(), "a notice expires");
  prompt.offer("x", false);
  check(prompt.view().offered && !prompt.view().affordable,
        "an unaffordable offer is shown but flagged");
  prompt.reset();
  check(!prompt.view().offered && prompt.view().text.empty(), "reset forgets");
}

/* Sparse guest memory: a read succeeds only over bytes that were written. */
class FakeGuest final : public x2::native::GuestMemoryView {
public:
  bool read(std::uint32_t address, void *out,
            std::size_t bytes) const override {
    auto *dst = static_cast<std::uint8_t *>(out);
    for (std::size_t i = 0; i < bytes; ++i) {
      const auto found = bytes_.find(address + static_cast<std::uint32_t>(i));
      if (found == bytes_.end()) {
        return false;
      }
      dst[i] = found->second;
    }
    return true;
  }
  void put(std::uint32_t address, const void *data, std::size_t bytes) {
    const auto *src = static_cast<const std::uint8_t *>(data);
    for (std::size_t i = 0; i < bytes; ++i) {
      bytes_[address + static_cast<std::uint32_t>(i)] = src[i];
    }
  }
  void u32(std::uint32_t address, std::uint32_t value) {
    put(address, &value, sizeof value);
  }
  void floats(std::uint32_t address, float x, float y, float z) {
    const float v[3] = {x, y, z};
    put(address, v, sizeof v);
  }
  void text(std::uint32_t address, const std::string &value) {
    std::string padded = value;
    padded.resize(64, '\0');
    put(address, padded.data(), padded.size());
  }

private:
  std::map<std::uint32_t, std::uint8_t> bytes_;
};

void entities() {
  using namespace x2::native;
  FakeGuest guest;
  const EntityTables tables{0x10000u, 0x20000u};
  /* Mask 3: handles 0x501 and 0x602 land in slots 1 and 2. */
  guest.u32(tables.manager + 0xc3cu, 3u);
  const std::uint32_t pad = 0x30000u;
  const std::uint32_t hero = 0x30100u;
  guest.u32(tables.manager + 4u + 4u * 1u, pad);
  guest.u32(tables.manager + 4u + 4u * 2u, hero);
  guest.u32(tables.manager + 4u + 4u * 0u, 0u);
  guest.u32(tables.manager + 4u + 4u * 3u, 0u);
  guest.u32(pad + 0x1cu, 0x501u);
  guest.u32(hero + 0x1cu, 0x602u);
  guest.u32(pad + 0x14u, 5u);
  guest.u32(hero + 0x14u, 6u);
  guest.floats(pad + 0x20u, 2580.5F, 2580.0F, 1.0F);
  guest.floats(hero + 0x20u, 10.0F, 20.0F, 30.0F);
  guest.u32(tables.pool + 4u + 5u * 4u, 0x00u);
  guest.u32(tables.pool + 4u + 6u * 4u, 0x40u);
  guest.text(tables.pool + 0x4008u + 0x00u, "xtraction_point");
  guest.text(tables.pool + 0x4008u + 0x40u, "hero_storm");

  check(pool_text(guest, tables, 5u) == "xtraction_point",
        "a pool handle resolves to its text");
  check(pool_text(guest, tables, 0u).empty(), "handle 0 has no text");
  check(pool_text(guest, tables, 99u).empty(), "an unmapped handle has none");

  const auto found = entity_by_handle(guest, tables, 0x501u);
  check(found && found->definition == "xtraction_point" &&
            found->at.x == 2580.5F && found->at.z == 1.0F,
        "an entity reads its definition and position");
  check(!entity_by_handle(guest, tables, 0x701u), "an empty slot is nothing");
  check(!entity_by_handle(guest, tables, 0x901u),
        "a handle whose slot holds another entity is stale");
  check(!entity_by_handle(guest, tables, 0u), "handle 0 is nothing");

  const std::vector<EntityView> all = all_entities(guest, tables);
  check(all.size() == 2 && all[0].definition == "xtraction_point" &&
            all[1].definition == "hero_storm" && all[1].at.y == 20.0F,
        "the whole table reads in slot order");

  const FakeGuest empty;
  check(all_entities(empty, tables).empty(),
        "an unreadable manager lists nothing");
}

} // namespace

int main() {
  cost_and_plan();
  prompt_requests();
  prompt_notice();
  entities();
  std::printf("test_extraction_revive: %d checks, %d failed\n", checks,
              failures);
  return failures == 0 ? 0 : 1;
}
