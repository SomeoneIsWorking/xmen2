#include "autosave_notice_document.hpp"

#include "autosave_notice.hpp"
#include "rml_text.hpp"

#include <RmlUi/Core.h>

namespace x2::ui {
namespace {

constexpr const char *kMarkup =
    "<rml><head><title>Autosave</title><style>"
    "body { width: 100%; height: 100%; margin: 0; overflow: hidden;"
    " pointer-events: none; font-family: LatoLatin; }"
    "#autosave { position: absolute; left: 40%; width: 20%; top: 24%;"
    " text-align: center;"
    " border: 2dp rgba(220, 238, 255, 80%); border-radius: 14dp;"
    " background-color: rgba(10, 18, 29, 70%); padding: 4dp 14dp;"
    " color: rgba(244, 249, 255, 96%); font-size: 16dp; font-weight: bold; }"
    "</style></head><body><div id='autosave'></div></body></rml>";

} // namespace

bool AutosaveNoticeDocument::load(Rml::Context *context) {
  document_ = context->LoadDocumentFromMemory(kMarkup);
  if (!document_) {
    return false;
  }
  label_ = document_->GetElementById("autosave");
  if (!label_) {
    return false;
  }
  document_->Hide();
  visible_ = false;
  return true;
}

void AutosaveNoticeDocument::shutdown() {
  document_ = nullptr;
  label_ = nullptr;
  visible_ = false;
}

bool AutosaveNoticeDocument::wanted() {
  return !save::autosave_notice().text().empty();
}

void AutosaveNoticeDocument::update() {
  if (!document_ || !label_) {
    return;
  }
  const std::string text = save::autosave_notice().text();
  const bool drawn = !text.empty();
  if (drawn != visible_) {
    visible_ = drawn;
    if (drawn) {
      document_->Show();
    } else {
      document_->Hide();
    }
  }
  if (drawn) {
    label_->SetInnerRML(escape_rml(text));
  }
}

} // namespace x2::ui
