#include "touch_controls.h"

#include <algorithm>
#include <array>

namespace x2::input {
namespace {

constexpr std::uint32_t left_stick = 1;
constexpr std::uint32_t right_stick = 2;

float clamp_unit(float value)
{
    return std::clamp(value, 0.0F, 1.0F);
}

float clamp_axis(float value)
{
    return std::clamp(value, -1.0F, 1.0F);
}

void add_button_events(std::vector<ActionEvent> &out,
                       const lucent::touch::Event &event,
                       TouchAction action)
{
    const float value = event.phase == lucent::touch::Phase::ended ||
                                event.phase == lucent::touch::Phase::canceled
                            ? 0.0F
                            : 1.0F;
    out.push_back({event.contact_id, action, value, event.phase});
}

void add_stick_events(std::vector<ActionEvent> &out,
                      const lucent::touch::Event &event,
                      const lucent::touch::Zone &zone,
                      const std::array<TouchAction, 4> &actions)
{
    const float center_x = (zone.left + zone.right) * 0.5F;
    const float center_y = (zone.top + zone.bottom) * 0.5F;
    const float radius_x = (zone.right - zone.left) * 0.5F;
    const float radius_y = (zone.bottom - zone.top) * 0.5F;
    const float horizontal = clamp_axis((event.position.x - center_x) / radius_x);
    const float vertical = clamp_axis((event.position.y - center_y) / radius_y);
    const bool release = event.phase == lucent::touch::Phase::ended ||
                         event.phase == lucent::touch::Phase::canceled;
    const std::array<float, 4> values = release
                                            ? std::array<float, 4>{0.0F, 0.0F, 0.0F, 0.0F}
                                            : std::array<float, 4>{
                                                  clamp_unit(-vertical), vertical,
                                                  clamp_unit(-horizontal), horizontal};
    for (std::size_t index = 0; index < actions.size(); ++index)
        out.push_back({event.contact_id, actions[index], values[index], event.phase});
}

} // namespace

std::vector<ActionEvent> TouchControls::set_viewport(Viewport viewport)
{
    auto released = cancel();
    viewport_ = viewport;
    rebuild_zones();
    return released;
}

void TouchControls::rebuild_zones()
{
    zones_.clear();
    const float left = viewport_.safe_area.left;
    const float top = viewport_.safe_area.top;
    const float right = viewport_.width - viewport_.safe_area.right;
    const float bottom = viewport_.height - viewport_.safe_area.bottom;
    const float width = right - left;
    const float height = bottom - top;
    if (width <= 0.0F || height <= 0.0F) {
        const std::vector<lucent::touch::Zone> empty;
        router_.set_zones(empty);
        return;
    }

    auto add = [this](std::uint32_t id, float zone_left, float zone_top,
                      float zone_right, float zone_bottom, int priority,
                      TouchAction action, bool stick) {
        zones_.push_back({{id, zone_left, zone_top, zone_right, zone_bottom, priority},
                          action, stick});
    };
    add(left_stick, left + width * 0.04F, top + height * 0.57F,
        left + width * 0.39F, bottom - height * 0.04F, 0, TouchAction::MoveLeft, true);
    const float right_stick_left = left + width * 0.61F;
    const float right_stick_top = top + height * 0.57F;
    const float right_stick_right = left + width * 0.96F;
    const float right_stick_bottom = bottom - height * 0.04F;
    add(right_stick, right_stick_left, right_stick_top, right_stick_right,
        right_stick_bottom, 0, TouchAction::CameraLeft, true);

    // Buttons have higher priority than the broad stick rectangles. Explicit gaps keep a thumb
    // on a face button from being interpreted as a camera movement.
    add(10, left + width * 0.75F, top + height * 0.29F,
        left + width * 0.84F, top + height * 0.41F, 10, TouchAction::LowAttack, false);
    add(11, left + width * 0.85F, top + height * 0.39F,
        left + width * 0.94F, top + height * 0.51F, 10, TouchAction::HighAttack, false);
    add(12, left + width * 0.65F, top + height * 0.39F,
        left + width * 0.74F, top + height * 0.51F, 10, TouchAction::Guard, false);
    add(13, left + width * 0.75F, top + height * 0.51F,
        left + width * 0.84F, top + height * 0.63F, 10, TouchAction::Jump, false);
    add(20, left + width * 0.63F, top + height * 0.08F,
        left + width * 0.74F, top + height * 0.19F, 10, TouchAction::Power, false);
    add(21, left + width * 0.76F, top + height * 0.08F,
        left + width * 0.87F, top + height * 0.19F, 10, TouchAction::Ally, false);
    add(22, left + width * 0.88F, top + height * 0.08F,
        left + width * 0.99F, top + height * 0.19F, 10, TouchAction::TargetLock, false);
    const float right_stick_center_x = (right_stick_left + right_stick_right) * 0.5F;
    const float right_stick_center_y = (right_stick_top + right_stick_bottom) * 0.5F;
    const float right_stick_click_size = std::min(width, height) * 0.09F;
    add(23, right_stick_center_x - right_stick_click_size,
        right_stick_center_y - right_stick_click_size,
        right_stick_center_x + right_stick_click_size,
        right_stick_center_y + right_stick_click_size, 20, TouchAction::MapToggle, false);
    add(30, left + width * 0.04F, top + height * 0.28F,
        left + width * 0.14F, top + height * 0.39F, 10, TouchAction::NextHero, false);
    add(31, left + width * 0.16F, top + height * 0.28F,
        left + width * 0.26F, top + height * 0.39F, 10, TouchAction::PreviousHero, false);
    add(32, left + width * 0.04F, top + height * 0.41F,
        left + width * 0.14F, top + height * 0.52F, 10, TouchAction::DecreaseAggr, false);
    add(33, left + width * 0.16F, top + height * 0.41F,
        left + width * 0.26F, top + height * 0.52F, 10, TouchAction::IncreaseAggr, false);
    add(40, left + width * 0.04F, top + height * 0.08F,
        left + width * 0.15F, top + height * 0.19F, 10, TouchAction::Pause, false);
    add(41, left + width * 0.17F, top + height * 0.08F,
        left + width * 0.28F, top + height * 0.19F, 10, TouchAction::Stats, false);

    std::vector<lucent::touch::Zone> router_zones;
    router_zones.reserve(zones_.size());
    for (const auto &zone : zones_)
        router_zones.push_back(zone.zone);
    router_.set_zones(router_zones);
}

std::vector<ActionEvent> TouchControls::route(
    std::span<const lucent::touch::Contact> contacts)
{
    const auto events = router_.route(contacts);
    return translate(events);
}

std::vector<ActionEvent> TouchControls::cancel()
{
    const auto events = router_.cancel();
    return translate(events);
}

std::vector<ActionEvent> TouchControls::translate(
    std::span<const lucent::touch::Event> events) const
{
    std::vector<ActionEvent> actions;
    actions.reserve(events.size() * 4);
    for (const auto &event : events) {
        const auto found = std::find_if(zones_.begin(), zones_.end(),
                                        [id = event.zone_id](const auto &zone) {
                                            return zone.zone.id == id;
                                        });
        if (found == zones_.end())
            continue;
        if (found->stick) {
            if (event.zone_id == left_stick) {
                add_stick_events(actions, event, found->zone,
                                 {TouchAction::Forward, TouchAction::Backward,
                                  TouchAction::MoveLeft, TouchAction::MoveRight});
            } else {
                add_stick_events(actions, event, found->zone,
                                 {TouchAction::CameraUp, TouchAction::CameraDown,
                                  TouchAction::CameraLeft, TouchAction::CameraRight});
            }
        } else {
            add_button_events(actions, event, found->action);
        }
    }
    return actions;
}

} // namespace x2::input
