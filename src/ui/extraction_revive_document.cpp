#include "extraction_revive_document.hpp"

#include "revive_prompt.hpp"
#include "rml_text.hpp"

#include <RmlUi/Core.h>

namespace x2::ui {
namespace {

/* The skip button's ring-and-fill look, with the offer's words. */
constexpr const char *kMarkup =
    "<rml><head><title>Revive</title><style>"
    "body { width: 100%; height: 100%; margin: 0; overflow: hidden;"
    " pointer-events: none; font-family: LatoLatin; }"
    "#revive { position: absolute; display: flex; flex-direction: column;"
    " align-items: center; justify-content: center; box-sizing: border-box;"
    " border: 2dp rgba(220, 238, 255, 80%); border-radius: 18dp;"
    " background-color: rgba(10, 18, 29, 70%); padding: 4dp 12dp; }"
    "#revive.short { border-color: rgba(255, 150, 140, 90%); }"
    "#revive-label { color: rgba(244, 249, 255, 96%); font-size: 18dp;"
    " font-weight: bold; text-align: center; }"
    "#revive.short #revive-label { color: rgba(255, 190, 180, 98%); }"
    "#revive-hint { color: rgba(200, 220, 240, 85%); font-size: 13dp;"
    " text-align: center; }"
    "</style></head><body>"
    "<div id='revive'><div id='revive-label'></div>"
    "<div id='revive-hint'></div></div>"
    "</body></rml>";

void set_percent(Rml::Element *element, Rml::PropertyId id, float value) {
  element->SetProperty(id, Rml::Property(value * 100.0F, Rml::Unit::PERCENT));
}

} // namespace

bool ExtractionReviveDocument::load(Rml::Context *context) {
  document_ = context->LoadDocumentFromMemory(kMarkup);
  if (!document_) {
    return false;
  }
  button_ = document_->GetElementById("revive");
  label_ = document_->GetElementById("revive-label");
  hint_ = document_->GetElementById("revive-hint");
  if (!button_ || !label_ || !hint_) {
    return false;
  }
  const input::PromptRect rect = input::kRevivePromptRect;
  set_percent(button_, Rml::PropertyId::Left, rect.left);
  set_percent(button_, Rml::PropertyId::Top, rect.top);
  set_percent(button_, Rml::PropertyId::Width, rect.right - rect.left);
  set_percent(button_, Rml::PropertyId::Height, rect.bottom - rect.top);
  document_->Hide();
  visible_ = false;
  return true;
}

void ExtractionReviveDocument::shutdown() {
  document_ = nullptr;
  button_ = nullptr;
  label_ = nullptr;
  hint_ = nullptr;
  visible_ = false;
}

bool ExtractionReviveDocument::wanted() {
  const input::RevivePromptView view = input::revive_prompt().view();
  return view.offered || !view.notice.empty();
}

void ExtractionReviveDocument::update() {
  if (!document_ || !label_ || !hint_) {
    return;
  }
  const input::RevivePromptView view = input::revive_prompt().view();
  const bool drawn = view.offered || !view.notice.empty();
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
  /* A fresh answer wins the line; the offer stays as the hint. */
  label_->SetInnerRML(
      escape_rml(view.notice.empty() ? view.text : view.notice));
  hint_->SetInnerRML(view.offered ? "F3 / LB / tap" : "");
  button_->SetClass("short", view.offered && !view.affordable);
}

} // namespace x2::ui
