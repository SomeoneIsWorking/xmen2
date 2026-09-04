#ifndef X2_IG_CONTROLLER_MANAGER_ADAPTER_HPP
#define X2_IG_CONTROLLER_MANAGER_ADAPTER_HPP

#include "directinput_controller_sample.h"

#include <alchemy/input/controller.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

namespace xmen2::input {

struct IgControllerManagerSettings {
  bool verifyAgainstDirectInput = true;
  float triggerButtonThreshold = 0.5F;
};

enum class DirectInputField : std::uint8_t {
  none,
  axis,
  pov,
  buttons,
  contract
};

struct DirectInputComparison {
  DirectInputField field = DirectInputField::none;
  std::uint8_t index = 0;
  std::int64_t retained = 0;
  std::int64_t shared = 0;

  [[nodiscard]] bool matches() const noexcept {
    return field == DirectInputField::none;
  }
};

/* The title owns how shared lifecycle events enter its recovered guest ABI.
 * Tests provide a recorder; the shipping bridge can leave this null until the
 * exact igControllerManager callback object is available. */
class GuestConnectionSink {
public:
  virtual ~GuestConnectionSink() = default;
  virtual void onGuestConnection(alchemy::input::ConnectionChange change,
                                 alchemy::input::SlotId slot,
                                 alchemy::input::DeviceId device) noexcept = 0;
};

/* Title-local conformance adapter between a latched PC controller sample and
 * the platform-neutral shared Alchemy owner. It never polls SDL, reads process
 * configuration, writes diagnostics, or owns guest addresses. */
class IgControllerManagerAdapter final
    : private alchemy::input::ConnectionObserver {
public:
  explicit IgControllerManagerAdapter(
      IgControllerManagerSettings settings,
      GuestConnectionSink *guestEvents = nullptr);

  [[nodiscard]] DirectInputComparison
  publish(std::size_t hostSlot, const X2DirectInputControllerSample &sample,
          std::int32_t axisLo, std::int32_t axisHi) noexcept;
  [[nodiscard]] DirectInputComparison
  compareAgainstDirectInput(alchemy::input::DeviceId device,
                            const X2DirectInputControllerSample &retained,
                            std::int32_t axisLo,
                            std::int32_t axisHi) const noexcept;
  void disconnectHostSlot(std::size_t hostSlot) noexcept;

  [[nodiscard]] const alchemy::input::ControllerManager &
  controllers() const noexcept;

private:
  void onConnectionEvent(
      const alchemy::input::ConnectionEvent &event) noexcept override;
  [[nodiscard]] bool synchronize(std::size_t hostSlot,
                                 alchemy::input::DeviceId device) noexcept;

  const IgControllerManagerSettings settings_;
  GuestConnectionSink *guestEvents_;
  alchemy::input::ControllerManager controllers_{this};
  std::array<alchemy::input::DeviceId, alchemy::input::kMaxControllerCount>
      hostDevices_{};
};

} // namespace xmen2::input

#endif
