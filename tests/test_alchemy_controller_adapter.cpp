#include "ig_controller_manager_adapter.hpp"

#include <alchemy/input/controller.hpp>

#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace {

class LifecycleRecorder final : public xmen2::input::GuestConnectionSink {
public:
  void onGuestConnection(alchemy::input::ConnectionChange change,
                         alchemy::input::SlotId slot,
                         alchemy::input::DeviceId device) noexcept override {
    changes[count] = change;
    slots[count] = slot;
    devices[count] = device;
    ++count;
  }

  std::array<alchemy::input::ConnectionChange, 8> changes{};
  std::array<alchemy::input::SlotId, 8> slots{};
  std::array<alchemy::input::DeviceId, 8> devices{};
  std::size_t count = 0;
};

X2DirectInputControllerSample sample(std::uint32_t device) {
  X2DirectInputControllerSample value{};
  value.device_id = device;
  value.axes[0] = -1000;
  value.axes[1] = 500;
  value.axes[2] = 750;
  value.axes[3] = 1000;
  value.axes[4] = -250;
  value.axes[5] = 0;
  value.pov = 4500;
  value.buttons = 0x03ff;
  value.left_trigger = 0.75F;
  value.right_trigger = 0.0F;
  return value;
}

} // namespace

int main() {
  using alchemy::input::Button;
  using alchemy::input::ConnectionChange;
  using alchemy::input::DeviceId;
  using xmen2::input::DirectInputField;
  using xmen2::input::IgControllerManagerAdapter;
  using xmen2::input::IgControllerManagerSettings;

  LifecycleRecorder lifecycle;
  IgControllerManagerAdapter adapter(IgControllerManagerSettings{true, 0.5F},
                                     &lifecycle);
  auto first = sample(41);
  assert(adapter.publish(0, first, -1000, 1000).matches());
  assert(lifecycle.count == 1);
  assert(lifecycle.changes[0] == ConnectionChange::connected);
  assert(lifecycle.slots[0].value == 0);
  assert(lifecycle.devices[0] == DeviceId{41});

  const auto *controller = adapter.controllers().find(DeviceId{41});
  assert(controller != nullptr);
  assert(controller->state.pressed(Button::faceDown));
  assert(controller->state.pressed(Button::faceRight));
  assert(controller->state.pressed(Button::faceLeft));
  assert(controller->state.pressed(Button::faceUp));
  assert(controller->state.pressed(Button::leftShoulder));
  assert(controller->state.pressed(Button::rightShoulder));
  assert(controller->state.pressed(Button::select));
  assert(controller->state.pressed(Button::start));
  assert(controller->state.pressed(Button::leftStick));
  assert(controller->state.pressed(Button::rightStick));
  assert(controller->state.pressed(Button::dpadUp));
  assert(controller->state.pressed(Button::dpadRight));
  assert(!controller->state.pressed(Button::dpadDown));
  assert(!controller->state.pressed(Button::dpadLeft));
  assert(controller->state.pressure(Button::leftTrigger) == 0.75F);
  assert(controller->state.pressure(Button::rightTrigger) == 0.0F);
  assert(controller->state.pressed(Button::leftTrigger));
  assert(!controller->state.pressed(Button::rightTrigger));

  /* Positive controls are insufficient for an A/B instrument. Mutate only
   * the retained side after publishing and prove the shipping comparison
   * reports the other answer and identifies the exact field. */
  auto divergent = first;
  divergent.axes[3] = 500;
  const auto mismatch =
      adapter.compareAgainstDirectInput(DeviceId{41}, divergent, -1000, 1000);
  assert(!mismatch.matches());
  assert(mismatch.field == DirectInputField::axis);
  assert(mismatch.index == 3);

  /* Replacing the physical identity in a host slot publishes a complete
   * disconnect/connect pair and reuses the stable shared slot. */
  auto replacement = sample(73);
  assert(adapter.publish(0, replacement, -1000, 1000).matches());
  assert(lifecycle.count == 3);
  assert(lifecycle.changes[1] == ConnectionChange::disconnected);
  assert(lifecycle.devices[1] == DeviceId{41});
  assert(lifecycle.changes[2] == ConnectionChange::connected);
  assert(lifecycle.devices[2] == DeviceId{73});
  assert(lifecycle.slots[2].value == 0);

  adapter.disconnectHostSlot(0);
  assert(lifecycle.count == 4);
  assert(lifecycle.changes[3] == ConnectionChange::disconnected);
  assert(adapter.controllers().size() == 0);

  std::puts("alchemy controller adapter: production mapping, negative A/B, and "
            "lifecycle passed");
  return 0;
}
