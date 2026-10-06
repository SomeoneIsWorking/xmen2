#include "ig_controller_manager_adapter.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace xmen2::input {
namespace {

using alchemy::input::Button;
using alchemy::input::ControllerState;

constexpr std::array<Button, X2_DIRECTINPUT_BUTTON_COUNT> kDirectInputButtons =
    {
        Button::faceDown,   Button::faceRight,    Button::faceLeft,
        Button::faceUp,     Button::leftShoulder, Button::rightShoulder,
        Button::select,     Button::start,        Button::leftStick,
        Button::rightStick,
};

float normalizeAxis(std::int32_t value, std::int32_t lo,
                    std::int32_t hi) noexcept {
  const std::int32_t midpoint = lo + (hi - lo) / 2;
  const float halfRange = static_cast<float>(hi - lo) / 2.0F;
  if (halfRange <= 0.0F) {
    return 0.0F;
  }
  return std::clamp(static_cast<float>(value - midpoint) / halfRange, -1.0F,
                    1.0F);
}

std::int32_t projectAxis(float value, std::int32_t lo,
                         std::int32_t hi) noexcept {
  const std::int32_t midpoint = lo + (hi - lo) / 2;
  const float halfRange = static_cast<float>(hi - lo) / 2.0F;
  return midpoint + static_cast<std::int32_t>(std::lround(
                        std::clamp(value, -1.0F, 1.0F) * halfRange));
}

void publishPov(ControllerState &state, std::uint32_t pov) noexcept {
  state.setPressed(Button::dpadUp, pov == 0u || pov == 4500u || pov == 31500u);
  state.setPressed(Button::dpadRight,
                   pov == 4500u || pov == 9000u || pov == 13500u);
  state.setPressed(Button::dpadDown,
                   pov == 13500u || pov == 18000u || pov == 22500u);
  state.setPressed(Button::dpadLeft,
                   pov == 22500u || pov == 27000u || pov == 31500u);
}

std::uint32_t projectPov(const ControllerState &state) noexcept {
  const bool up = state.pressed(Button::dpadUp);
  const bool right = state.pressed(Button::dpadRight);
  const bool down = state.pressed(Button::dpadDown);
  const bool left = state.pressed(Button::dpadLeft);
  if (up && !left && !right) {
    return 0u;
  }
  if (up && right) {
    return 4500u;
  }
  if (right && !up && !down) {
    return 9000u;
  }
  if (down && right) {
    return 13500u;
  }
  if (down && !left && !right) {
    return 18000u;
  }
  if (down && left) {
    return 22500u;
  }
  if (left && !up && !down) {
    return 27000u;
  }
  if (up && left) {
    return 31500u;
  }
  return UINT32_MAX;
}

ControllerState toAlchemyState(const X2DirectInputControllerSample &sample,
                               std::int32_t lo, std::int32_t hi,
                               float triggerThreshold) noexcept {
  ControllerState state;
  for (std::size_t index = 0; index < kDirectInputButtons.size(); ++index) {
    const bool pressed = (sample.buttons & (std::uint16_t{1} << index)) != 0;
    state.setPressed(kDirectInputButtons[index], pressed);
    state.setPressure(kDirectInputButtons[index], pressed ? 1.0F : 0.0F);
  }
  publishPov(state, sample.pov);
  state.setStick(0, {normalizeAxis(sample.axes[0], lo, hi),
                     normalizeAxis(sample.axes[1], lo, hi)});
  state.setStick(1, {normalizeAxis(sample.axes[3], lo, hi),
                     normalizeAxis(sample.axes[4], lo, hi)});
  state.setPressure(Button::leftTrigger, sample.left_trigger);
  state.setPressure(Button::rightTrigger, sample.right_trigger);
  state.setPressed(Button::leftTrigger, sample.left_trigger > triggerThreshold);
  state.setPressed(Button::rightTrigger,
                   sample.right_trigger > triggerThreshold);
  return state;
}

DirectInputComparison compareState(const X2DirectInputControllerSample &sample,
                                   const ControllerState &state,
                                   std::int32_t lo, std::int32_t hi) noexcept {
  const auto left = state.stick(0);
  const auto right = state.stick(1);
  const std::int32_t midpoint = lo + (hi - lo) / 2;
  const std::array<std::int32_t, X2_DIRECTINPUT_AXIS_COUNT> projected = {
      projectAxis(left.x, lo, hi),
      projectAxis(left.y, lo, hi),
      projectAxis(state.pressure(Button::leftTrigger) -
                      state.pressure(Button::rightTrigger),
                  lo, hi),
      projectAxis(right.x, lo, hi),
      projectAxis(right.y, lo, hi),
      midpoint,
  };
  for (std::size_t index = 0; index < projected.size(); ++index) {
    if (std::abs(projected[index] - sample.axes[index]) > 1) {
      return {DirectInputField::axis, static_cast<std::uint8_t>(index),
              sample.axes[index], projected[index]};
    }
  }

  const std::uint32_t pov = projectPov(state);
  if (pov != sample.pov) {
    return {DirectInputField::pov, 0, sample.pov, pov};
  }

  std::uint16_t buttons = 0;
  for (std::size_t index = 0; index < kDirectInputButtons.size(); ++index) {
    if (state.pressed(kDirectInputButtons[index])) {
      buttons |= std::uint16_t{1} << index;
    }
  }
  if (buttons != sample.buttons) {
    return {DirectInputField::buttons, 0, sample.buttons, buttons};
  }
  return {};
}

} // namespace

IgControllerManagerAdapter::IgControllerManagerAdapter(
    IgControllerManagerSettings settings, GuestConnectionSink *guestEvents)
    : settings_(settings), guestEvents_(guestEvents) {}

DirectInputComparison IgControllerManagerAdapter::publish(
    std::size_t hostSlot, const X2DirectInputControllerSample &sample,
    std::int32_t axisLo, std::int32_t axisHi) noexcept {
  const alchemy::input::DeviceId device{sample.device_id};
  if (hostSlot >= hostDevices_.size() || !device || axisHi <= axisLo ||
      !synchronize(hostSlot, device)) {
    return {DirectInputField::contract, 0, 1, 0};
  }

  const ControllerState state =
      toAlchemyState(sample, axisLo, axisHi, settings_.triggerButtonThreshold);
  if (!controllers_.publish(device, state)) {
    return {DirectInputField::contract, 0, 1, 0};
  }
  if (!settings_.verifyAgainstDirectInput) {
    return {};
  }
  return compareAgainstDirectInput(device, sample, axisLo, axisHi);
}

DirectInputComparison IgControllerManagerAdapter::compareAgainstDirectInput(
    alchemy::input::DeviceId device,
    const X2DirectInputControllerSample &retained, std::int32_t axisLo,
    std::int32_t axisHi) const noexcept {
  const auto *controller = controllers_.find(device);
  return controller != nullptr
             ? compareState(retained, controller->state, axisLo, axisHi)
             : DirectInputComparison{DirectInputField::contract, 0, 1, 0};
}

void IgControllerManagerAdapter::disconnectHostSlot(
    std::size_t hostSlot) noexcept {
  if (hostSlot >= hostDevices_.size() || !hostDevices_[hostSlot]) {
    return;
  }
  static_cast<void>(controllers_.disconnect(hostDevices_[hostSlot]));
  hostDevices_[hostSlot] = {};
}

const alchemy::input::ControllerManager &
IgControllerManagerAdapter::controllers() const noexcept {
  return controllers_;
}

void IgControllerManagerAdapter::onConnectionEvent(
    const alchemy::input::ConnectionEvent &event) noexcept {
  if (guestEvents_ != nullptr) {
    guestEvents_->onGuestConnection(event.change, event.controller.slot,
                                    event.controller.device.id);
  }
}

bool IgControllerManagerAdapter::synchronize(
    std::size_t hostSlot, alchemy::input::DeviceId device) noexcept {
  if (hostDevices_[hostSlot] == device) {
    return controllers_.find(device) != nullptr;
  }
  disconnectHostSlot(hostSlot);
  const auto result = controllers_.connect(
      {device, alchemy::input::ControllerType::xbox360Microsoft10ButtonsPov,
       true});
  if (result.status != alchemy::input::ConnectStatus::connected) {
    return false;
  }
  hostDevices_[hostSlot] = device;
  return true;
}

} // namespace xmen2::input
