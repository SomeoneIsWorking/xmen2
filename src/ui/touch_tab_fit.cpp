#include "touch_tab_fit.hpp"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cctype>

namespace x2::ui {

void fit_tab_labels(Rml::ElementDocument *document,
                    const std::vector<std::string> &labels) {
  Rml::ElementList tabs;
  document->GetElementsByClassName(tabs, "tm-tab");
  if (tabs.empty()) {
    return;
  }
  document->UpdateDocument();
  std::vector<Rml::Element *> fitted;
  float fit = 1.0F;
  for (std::size_t i = 0; i < tabs.size() && i < labels.size(); ++i) {
    Rml::Element *label = tabs[i]->GetFirstChild();
    if (label == nullptr) {
      continue;
    }
    std::string text = labels[i];
    std::transform(text.begin(), text.end(), text.begin(), [](char c) {
      return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    });
    const auto width =
        static_cast<float>(Rml::ElementUtilities::GetStringWidth(label, text));
    const float room = tabs[i]->GetBox().GetSize(Rml::BoxArea::Content).x;
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
