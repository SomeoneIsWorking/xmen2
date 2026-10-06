#include "skip_document.hpp"

#include "touch_runtime.h"

#include <RmlUi/Core.h>

namespace x2::ui {
namespace {

/* The same ring and fill as a touch overlay button (touch_controls.rcss), as
   a pill that carries its word, since a cinematic has no icon to mean
   "skip". */
constexpr const char *kMarkup =
    "<rml><head><title>Skip</title><style>"
    "body { width: 100%; height: 100%; margin: 0; overflow: hidden;"
    " pointer-events: none; font-family: LatoLatin; }"
    "#skip { position: absolute; display: flex; align-items: center;"
    " justify-content: center; box-sizing: border-box;"
    " border: 2dp rgba(220, 238, 255, 80%); border-radius: 512dp;"
    " background-color: rgba(10, 18, 29, 55%); }"
    "#skip.active { border-color: #FFFFFF;"
    " background-color: rgba(255, 255, 255, 30%); }"
    "#skip-label { color: rgba(244, 249, 255, 96%); font-size: 18dp;"
    " font-weight: bold; text-transform: uppercase; letter-spacing: 1dp; }"
    "</style></head><body>"
    "<div id='skip'><span id='skip-label'>Skip &#187;</span></div>"
    "</body></rml>";

void set_percent(Rml::Element *element, Rml::PropertyId id, float value) {
  element->SetProperty(id, Rml::Property(value, Rml::Unit::PERCENT));
}

} // namespace

bool SkipDocument::load(Rml::Context *context) {
  document_ = context->LoadDocumentFromMemory(kMarkup);
  if (!document_) {
    return false;
  }
  button_ = document_->GetElementById("skip");
  if (!button_) {
    return false;
  }
  document_->Hide();
  visible_ = false;
  return true;
}

void SkipDocument::shutdown() {
  document_ = nullptr;
  button_ = nullptr;
  visible_ = false;
}

bool SkipDocument::wanted() {
  return x2_touch_runtime_skip_button(nullptr, nullptr) != 0;
}

void SkipDocument::update() {
  if (!document_ || !button_) {
    return;
  }
  X2Rect rect{};
  int held = 0;
  const bool drawn = x2_touch_runtime_skip_button(&rect, &held) != 0;
  if (drawn != visible_) {
    visible_ = drawn;
    if (drawn) {
      document_->Show();
    } else {
      document_->Hide();
    }
  }
  if (!drawn) {
    return;
  }
  const Rml::Vector2i dimensions = document_->GetContext()->GetDimensions();
  if (dimensions.x <= 0 || dimensions.y <= 0) {
    return;
  }
  const float width = static_cast<float>(dimensions.x);
  const float height = static_cast<float>(dimensions.y);
  set_percent(button_, Rml::PropertyId::Left, rect.left * 100.0F / width);
  set_percent(button_, Rml::PropertyId::Top, rect.top * 100.0F / height);
  set_percent(button_, Rml::PropertyId::Width,
              (rect.right - rect.left) * 100.0F / width);
  set_percent(button_, Rml::PropertyId::Height,
              (rect.bottom - rect.top) * 100.0F / height);
  button_->SetClass("active", held != 0);
}

} // namespace x2::ui
