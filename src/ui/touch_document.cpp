/* Visual feedback for the title-owned touch layout. Contact acquisition and
 * action publication remain in input/; this document only mirrors the
 * production zones and pressed state into RmlUi. */
#include "touch_document.hpp"

#include "touch_controls.h"
#include "touch_runtime.h"
#include "ui_resources.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace x2::ui {
namespace {

Rml::ElementDocument *document;
Rml::Element *root;
std::vector<X2TouchVisual> visuals;
bool document_visible;

const char *icon_name(int action, bool stick)
{
    using x2::input::TouchAction;
    if (stick)
        return action == static_cast<int>(TouchAction::MoveLeft) ? "ls.svg" : "rs.svg";
    switch (static_cast<TouchAction>(action)) {
    case TouchAction::LowAttack: return "face_a.svg";
    case TouchAction::HighAttack: return "face_b.svg";
    case TouchAction::Guard: return "face_x.svg";
    case TouchAction::Jump: return "face_y.svg";
    case TouchAction::Power: return "rt.svg";
    case TouchAction::Ally: return "lt.svg";
    case TouchAction::TargetLock: return "rb.svg";
    case TouchAction::NextHero: return "dpad_up.svg";
    case TouchAction::PreviousHero: return "dpad_down.svg";
    case TouchAction::DecreaseAggr: return "dpad_left.svg";
    case TouchAction::IncreaseAggr: return "dpad_right.svg";
    case TouchAction::MapToggle: return "rs.svg";
    case TouchAction::Pause: return "start.svg";
    case TouchAction::Stats: return "back.svg";
    default: return "face_a.svg";
    }
}

std::string resource(const std::string &relative)
{
    return x2_ui_resource_path(relative.c_str());
}

void rebuild()
{
    const size_t count = x2_touch_runtime_visuals(nullptr, 0);
    visuals.resize(count);
    x2_touch_runtime_visuals(visuals.data(), visuals.size());
    std::ostringstream rml;
    for (const auto &visual : visuals) {
        const char *icon = icon_name(visual.action, visual.stick != 0);
        rml << "<div id='touch-zone-" << visual.id << "' class='touch-zone"
            << (visual.stick ? " stick" : "") << "'><img src='"
            << resource(std::string("touch/") + icon) << "' /></div>";
    }
    if (root) root->SetInnerRML(rml.str());
}

void set_percent(Rml::Element *element, Rml::PropertyId property, float value)
{
    element->SetProperty(property, Rml::Property(value, Rml::Unit::PERCENT));
}

} // namespace

bool touch_document_load(Rml::Context *context)
{
    const std::string style = resource("touch_controls.rcss");
    const std::string base = resource("touch_controls.rml");
    const std::string shell =
        "<rml><head><title>Touch Controls</title><link type='text/rcss' href='" +
        style + "' /></head><body id='touch-root'></body></rml>";
    document = context->LoadDocumentFromMemory(shell, base);
    if (!document) return false;
    root = document->GetElementById("touch-root");
    if (!root) return false;
    rebuild();
    document->Hide();
    return true;
}

void touch_document_shutdown()
{
    document = nullptr;
    root = nullptr;
    visuals.clear();
    document_visible = false;
}

void touch_document_set_visible(bool visible)
{
    if (!document || visible == document_visible) return;
    document_visible = visible;
    if (visible) document->Show();
    else document->Hide();
}

void touch_document_update()
{
    if (!document || !document_visible) return;
    const size_t count = x2_touch_runtime_visuals(nullptr, 0);
    if (count != visuals.size()) rebuild();
    if (visuals.empty()) return;
    x2_touch_runtime_visuals(visuals.data(), visuals.size());
    const Rml::Vector2i dimensions = document->GetContext()->GetDimensions();
    if (dimensions.x <= 0 || dimensions.y <= 0) return;
    for (const auto &visual : visuals) {
        Rml::Element *element = document->GetElementById(
            "touch-zone-" + std::to_string(visual.id));
        if (!element) continue;
        const float width = static_cast<float>(dimensions.x);
        const float height = static_cast<float>(dimensions.y);
        set_percent(element, Rml::PropertyId::Left, visual.left * 100.0F / width);
        set_percent(element, Rml::PropertyId::Top, visual.top * 100.0F / height);
        set_percent(element, Rml::PropertyId::Width,
                    (visual.right - visual.left) * 100.0F / width);
        set_percent(element, Rml::PropertyId::Height,
                    (visual.bottom - visual.top) * 100.0F / height);
        element->SetClass("active", visual.active != 0);
    }
}

} // namespace x2::ui
