#include "../src/input/touch_controls.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

bool has_value(const std::vector<x2::input::ActionEvent> &events,
               x2::input::TouchAction action, float minimum)
{
    return std::any_of(events.begin(), events.end(), [action, minimum](const auto &event) {
        return event.action == action && event.value >= minimum;
    });
}

} // namespace

int main()
{
    x2::input::TouchControls controls;
    controls.set_viewport({1000.0F, 600.0F, {20.0F, 10.0F, 20.0F, 10.0F}});

    const std::vector<lucent::touch::Contact> stick_down = {
        {1, {226.0F, 456.0F}, lucent::touch::Phase::began}};
    const auto began = controls.route(stick_down);
    if (began.size() != 4 || has_value(began, x2::input::TouchAction::Forward, 0.01F) ||
        has_value(began, x2::input::TouchAction::MoveLeft, 0.01F)) {
        std::cerr << "left stick did not begin with neutral directional state\n";
        return 1;
    }

    const std::vector<lucent::touch::Contact> stick_up = {
        {1, {150.0F, 300.0F}, lucent::touch::Phase::moved}};
    const auto moved = controls.route(stick_up);
    if (!has_value(moved, x2::input::TouchAction::Forward, 0.5F) ||
        !has_value(moved, x2::input::TouchAction::MoveLeft, 0.01F)) {
        std::cerr << "left stick did not produce the expected action values\n";
        return 1;
    }
    const auto left_y = x2::input::touch_axis_value(
        moved, x2::input::TouchAction::Forward,
        x2::input::TouchAction::Backward);
    const auto left_x = x2::input::touch_axis_value(
        moved, x2::input::TouchAction::MoveLeft,
        x2::input::TouchAction::MoveRight);
    if (!left_y || *left_y >= -0.5F || !left_x || *left_x >= -0.01F) {
        std::cerr << "touch controls: directional events did not compose into signed axes\n";
        return 1;
    }

    const std::vector<lucent::touch::Contact> button = {
        {2, {800.0F, 230.0F}, lucent::touch::Phase::began}};
    const auto button_events = controls.route(button);
    if (!has_value(button_events, x2::input::TouchAction::LowAttack, 1.0F)) {
        std::cerr << "face-button zone was not reachable\n";
        return 1;
    }

    const std::vector<lucent::touch::Contact> map_button = {
        {3, {775.0F, 456.0F}, lucent::touch::Phase::began}};
    const auto map_button_events = controls.route(map_button);
    if (!has_value(map_button_events, x2::input::TouchAction::MapToggle, 1.0F)) {
        std::cerr << "right-stick click zone was not reachable\n";
        return 1;
    }

    const auto canceled = controls.cancel();
    if (canceled.size() != 6 ||
        !std::all_of(canceled.begin(), canceled.end(), [](const auto &event) {
            return event.value == 0.0F && event.phase == lucent::touch::Phase::canceled;
        })) {
        std::cerr << "cancel did not release the captured controls (" << canceled.size()
                  << " events)\n";
        return 1;
    }

    const std::vector<lucent::touch::Contact> held_button = {
        {4, {800.0F, 230.0F}, lucent::touch::Phase::began}};
    controls.route(held_button);
    const auto rotated = controls.set_viewport(
        {600.0F, 1000.0F, {10.0F, 20.0F, 10.0F, 20.0F}});
    if (rotated.size() != 1 || rotated.front().action != x2::input::TouchAction::LowAttack ||
        rotated.front().value != 0.0F ||
        rotated.front().phase != lucent::touch::Phase::canceled) {
        std::cerr << "viewport change did not release the old layout\n";
        return 1;
    }
    std::cout << "touch controls: layout, action mapping, and cancellation passed\n";
    return 0;
}
