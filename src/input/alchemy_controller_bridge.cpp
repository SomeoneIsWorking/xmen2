#include "alchemy_controller_bridge.h"

#include "dinput_pad.h"
#include "ig_controller_manager_adapter.hpp"

#include <lucent/cvar_c.h>
#include <lucent/log_c.h>

#include <array>
#include <cstdint>

namespace {

using xmen2::input::DirectInputComparison;
using xmen2::input::DirectInputField;
using xmen2::input::IgControllerManagerAdapter;
using xmen2::input::IgControllerManagerSettings;

const char *fieldName(DirectInputField field) noexcept {
  switch (field) {
  case DirectInputField::axis:
    return "axis";
  case DirectInputField::pov:
    return "pov";
  case DirectInputField::buttons:
    return "buttons";
  case DirectInputField::contract:
    return "contract";
  case DirectInputField::none:
    return "none";
  }
  return "unknown";
}

class AlchemyControllerBridge final {
public:
  AlchemyControllerBridge()
      : verificationEnabled_(lucent_cvar_flag("alchemy.input.verify", 1) != 0),
        adapter_(IgControllerManagerSettings{verificationEnabled_, 0.5F}) {}

  void synchronizeInventory() noexcept {
    for (std::size_t slot = 0; slot < devices_.size(); ++slot) {
      const std::uint32_t current =
          dinput_pad_device_id(static_cast<int>(slot));
      if (current == devices_[slot]) {
        continue;
      }
      if (devices_[slot] != 0) {
        adapter_.disconnectHostSlot(slot);
        ++disconnects_;
      }
      devices_[slot] = current;
      if (current != 0) {
        ++connects_;
      }
    }
  }

  void observe(int hostSlot, const X2DirectInputControllerSample &sample,
               std::int32_t lo, std::int32_t hi) noexcept {
    if (hostSlot < 0 || static_cast<std::size_t>(hostSlot) >= devices_.size()) {
      return;
    }
    synchronizeInventory();
    ++observations_;
    const DirectInputComparison comparison =
        adapter_.publish(static_cast<std::size_t>(hostSlot), sample, lo, hi);
    if (!verificationEnabled_) {
      return;
    }
    ++comparisons_;
    if (comparison.matches()) {
      ++matches_;
      return;
    }
    ++mismatches_;
    lucent_log_error(
        "alchemy.input",
        "DirectInput A/B mismatch on pad %d %s[%u]: retained=%lld shared=%lld",
        hostSlot, fieldName(comparison.field), comparison.index,
        static_cast<long long>(comparison.retained),
        static_cast<long long>(comparison.shared));
  }

  void report() const noexcept {
    lucent_log_info(
        "alchemy.input",
        "shared igControllerManager adapter: %llu observation(s), A/B %s, "
        "%llu comparison(s), %llu match, %llu mismatch, %llu connect "
        "transition(s), %llu disconnect transition(s)",
        static_cast<unsigned long long>(observations_),
        verificationEnabled_ ? "enabled" : "disabled",
        static_cast<unsigned long long>(comparisons_),
        static_cast<unsigned long long>(matches_),
        static_cast<unsigned long long>(mismatches_),
        static_cast<unsigned long long>(connects_),
        static_cast<unsigned long long>(disconnects_));
  }

private:
  const bool verificationEnabled_;
  IgControllerManagerAdapter adapter_;
  std::array<std::uint32_t, alchemy::input::kMaxControllerCount> devices_{};
  std::uint64_t observations_ = 0;
  std::uint64_t comparisons_ = 0;
  std::uint64_t matches_ = 0;
  std::uint64_t mismatches_ = 0;
  std::uint64_t connects_ = 0;
  std::uint64_t disconnects_ = 0;
};

AlchemyControllerBridge &bridge() {
  static AlchemyControllerBridge instance;
  return instance;
}

} // namespace

extern "C" void
x2_alchemy_controller_observe(int host_slot,
                              const X2DirectInputControllerSample *sample,
                              int32_t axis_lo, int32_t axis_hi) {
  if (sample != nullptr) {
    bridge().observe(host_slot, *sample, axis_lo, axis_hi);
  }
}

extern "C" void x2_alchemy_controller_sync_inventory(void) {
  bridge().synchronizeInventory();
}

extern "C" void x2_alchemy_controller_report(void) { bridge().report(); }
