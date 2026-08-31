#include "touch_controls.h"

#include <algorithm>
#include <array>

namespace x2::input {
namespace {

constexpr std::uint32_t left_stick = 1;
constexpr std::uint32_t camera_swipe = 2;

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
    out.push_back({event.contact_id, event.zone_id, action, value,
                   event.position, event.phase});
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
                                                  clamp_unit(-vertical), clamp_unit(vertical),
                                                  clamp_unit(-horizontal), clamp_unit(horizontal)};
    for (std::size_t index = 0; index < actions.size(); ++index)
        out.push_back({event.contact_id, event.zone_id, actions[index], values[index],
                       event.position, event.phase});
}

void add_camera_events(std::vector<ActionEvent> &out,
                       const lucent::touch::Event &event,
                       const lucent::touch::Zone &zone)
{
    const float travel = std::min(zone.right - zone.left,
                                  zone.bottom - zone.top) * 0.22F;
    const bool release = event.phase == lucent::touch::Phase::ended ||
                         event.phase == lucent::touch::Phase::canceled;
    const float horizontal = release || travel <= 0.0F
                                 ? 0.0F
                                 : clamp_axis((event.position.x - event.origin.x) / travel);
    const float vertical = release || travel <= 0.0F
                               ? 0.0F
                               : clamp_axis((event.position.y - event.origin.y) / travel);
    const std::array<TouchAction, 4> actions = {
        TouchAction::CameraUp, TouchAction::CameraDown,
        TouchAction::CameraLeft, TouchAction::CameraRight};
    const std::array<float, 4> values = {
        clamp_unit(-vertical), clamp_unit(vertical),
        clamp_unit(-horizontal), clamp_unit(horizontal)};
    for (std::size_t index = 0; index < actions.size(); ++index)
        out.push_back({event.contact_id, event.zone_id, actions[index], values[index],
                       event.position, event.phase});
}

} // namespace

std::optional<float> touch_axis_value(
    std::span<const ActionEvent> events, TouchAction negative,
    TouchAction positive)
{
    std::optional<float> negative_value;
    std::optional<float> positive_value;
    for (const auto &event : events) {
        if (event.action == negative) negative_value = event.value;
        else if (event.action == positive) positive_value = event.value;
    }
    if (!negative_value && !positive_value) return std::nullopt;
    return clamp_axis(positive_value.value_or(0.0F) -
                      negative_value.value_or(0.0F));
}

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
                      TouchAction action, bool stick, bool visible = true) {
        zones_.push_back({{id, zone_left, zone_top, zone_right, zone_bottom, priority},
                          action, stick, visible});
    };
    add(left_stick, left + width * 0.04F, top + height * 0.57F,
        left + width * 0.39F, bottom - height * 0.04F, 0, TouchAction::MoveLeft, true);

    // Camera is a relative swipe over otherwise unused playfield. Buttons and
    // retail portrait taps capture first, so a combat chord never moves it.
    add(camera_swipe, left + width * 0.35F, top + height * 0.18F,
        left + width * 0.98F, top + height * 0.62F, -10,
        TouchAction::CameraLeft, false, false);

    add(10, left + width * 0.77F, top + height * 0.70F,
        left + width * 0.87F, top + height * 0.83F, 20,
        TouchAction::LightAttack, false);
    add(11, left + width * 0.88F, top + height * 0.61F,
        left + width * 0.98F, top + height * 0.74F, 20,
        TouchAction::HeavyAttack, false);
    add(12, left + width * 0.66F, top + height * 0.61F,
        left + width * 0.76F, top + height * 0.74F, 20,
        TouchAction::Use, false);
    add(13, left + width * 0.77F, top + height * 0.52F,
        left + width * 0.87F, top + height * 0.65F, 20,
        TouchAction::Jump, false);
    add(20, left + width * 0.66F, top + height * 0.78F,
        left + width * 0.76F, top + height * 0.91F, 20,
        TouchAction::Powers, false);
    add(21, left + width * 0.77F, top + height * 0.85F,
        left + width * 0.87F, top + height * 0.98F, 20,
        TouchAction::EnergyPack, false);
    add(22, left + width * 0.88F, top + height * 0.78F,
        left + width * 0.98F, top + height * 0.91F, 20,
        TouchAction::HealthPack, false);
    add(23, left + width * 0.61F, top + height * 0.04F,
        left + width * 0.69F, top + height * 0.15F, 20,
        TouchAction::MapToggle, false);
    add(32, left + width * 0.43F, top + height * 0.84F,
        left + width * 0.51F, top + height * 0.95F, 20,
        TouchAction::DecreaseAggr, false);
    add(33, left + width * 0.52F, top + height * 0.84F,
        left + width * 0.60F, top + height * 0.95F, 20,
        TouchAction::IncreaseAggr, false);
    add(40, left + width * 0.43F, top + height * 0.04F,
        left + width * 0.51F, top + height * 0.15F, 20,
        TouchAction::Pause, false);
    add(41, left + width * 0.52F, top + height * 0.04F,
        left + width * 0.60F, top + height * 0.15F, 20,
        TouchAction::Stats, false);

    // The retail party cross is relocated to this top-right cluster in touch
    // mode. These zones emit pointer events so its existing click handler,
    // including direct hero selection, remains the only behavior owner.
    const float portrait_radius = std::min(width, height) * 0.07F;
    const float portrait_center_x = right - portrait_radius * 2.0F;
    const float portrait_center_y = top + portrait_radius * 2.0F;
    auto portrait = [&](std::uint32_t id, float center_x, float center_y,
                        TouchAction action) {
        add(id, center_x - portrait_radius, center_y - portrait_radius,
            center_x + portrait_radius, center_y + portrait_radius, 30,
            action, false, false);
    };
    portrait(50, portrait_center_x, portrait_center_y - portrait_radius,
             TouchAction::SelectHero1);
    portrait(51, portrait_center_x + portrait_radius, portrait_center_y,
             TouchAction::SelectHero2);
    portrait(52, portrait_center_x, portrait_center_y + portrait_radius,
             TouchAction::SelectHero3);
    portrait(53, portrait_center_x - portrait_radius, portrait_center_y,
             TouchAction::SelectHero4);

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
            }
        } else if (event.zone_id == camera_swipe) {
            add_camera_events(actions, event, found->zone);
        } else {
            add_button_events(actions, event, found->action);
        }
    }
    return actions;
}

} // namespace x2::input
