#include "touch_label_fit.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cctype>

namespace x2::ui {

void fit_button_labels(Rml::ElementDocument *document,
                       const std::string &button_class,
                       const std::vector<std::string> &labels) {
  Rml::ElementList buttons;
  document->GetElementsByClassName(buttons, button_class);
  if (buttons.empty()) {
    return;
  }
  document->UpdateDocument();
  std::vector<Rml::Element *> fitted;
  float fit = 1.0F;
  for (std::size_t i = 0; i < buttons.size() && i < labels.size(); ++i) {
    Rml::Element *label = buttons[i]->GetFirstChild();
    if (label == nullptr) {
      continue;
    }
    std::string text = labels[i];
    std::transform(text.begin(), text.end(), text.begin(), [](char c) {
      return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    const auto width =
        static_cast<float>(Rml::ElementUtilities::GetStringWidth(label, text));
    const float room = buttons[i]->GetBox().GetSize(Rml::BoxArea::Content).x;
    if (width > room && width > 0.0F) {
      fit = std::min(fit, room / width);
    }
    fitted.push_back(label);
  }
  if (fit >= 1.0F) {
    return;
  }
  for (Rml::Element *label : fitted) {
    label->SetProperty(
        Rml::PropertyId::FontSize,
        Rml::Property(label->GetComputedValues().font_size() * fit,
                      Rml::Unit::PX));
  }
}

} // namespace x2::ui
