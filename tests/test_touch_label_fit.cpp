/* fit_button_labels on a real RmlUi layout with the shipped stylesheet and
   font. */
#include "touch_label_fit.hpp"

#include <RmlUi/Core.h>

#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char *what) {
  ++checks;
  if (!ok) {
    ++failures;
    std::printf("FAIL: %s\n", what);
  }
}

/* Layout and text measurement only; nothing is drawn. */
class NullRender : public Rml::RenderInterface {
public:
  Rml::CompiledGeometryHandle
  CompileGeometry(Rml::Span<const Rml::Vertex> /*vertices*/,
                  Rml::Span<const int> /*indices*/) override {
    return 1;
  }
  void RenderGeometry(Rml::CompiledGeometryHandle /*geometry*/,
                      Rml::Vector2f /*translation*/,
                      Rml::TextureHandle /*texture*/) override {}
  void ReleaseGeometry(Rml::CompiledGeometryHandle /*geometry*/) override {}
  Rml::TextureHandle LoadTexture(Rml::Vector2i & /*dimensions*/,
                                 const Rml::String & /*source*/) override {
    return 0;
  }
  Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> /*source*/,
                                     Rml::Vector2i /*dimensions*/) override {
    return 1;
  }
  void ReleaseTexture(Rml::TextureHandle /*texture*/) override {}
  void EnableScissorRegion(bool /*enable*/) override {}
  void SetScissorRegion(Rml::Rectanglei /*region*/) override {}
};

std::string button_row(const std::string &button_class,
                       const std::vector<std::string> &labels,
                       float button_width, int root_px = 30) {
  std::string body;
  for (std::size_t i = 0; i < labels.size(); ++i) {
    body += "<div class='tm-button " + button_class + "' style='left:" +
            std::to_string(static_cast<float>(i) * button_width) +
            "px;top:0px;width:" + std::to_string(button_width) +
            "px;height:60px;'><span class='tm-label'>" + labels[i] +
            "</span></div>";
  }
  return "<rml><head><link type='text/rcss' href='touch_menu.rcss' /></head>"
         "<body style='font-size:" +
         std::to_string(root_px) + "px;'>" + body + "</body></rml>";
}

std::string tab_bar(float tab_width) {
  return button_row("tm-tab", {"screens", "cinematics", "stats"}, tab_width);
}

struct Measured {
  float widest_overflow = 0.0F;
  std::vector<float> sizes;
  /* The tallest label over its own line height: 1 for one line. */
  float most_lines = 0.0F;
};

Measured measure(Rml::ElementDocument *document, const char *button_class,
                 const std::vector<std::string> &upper) {
  document->UpdateDocument();
  Rml::ElementList buttons;
  document->GetElementsByClassName(buttons, button_class);
  Measured out;
  for (std::size_t i = 0; i < buttons.size(); ++i) {
    Rml::Element *label = buttons[i]->GetFirstChild();
    const auto width = static_cast<float>(
        Rml::ElementUtilities::GetStringWidth(label, upper[i]));
    out.widest_overflow =
        std::max(out.widest_overflow,
                 width - buttons[i]->GetBox().GetSize(Rml::BoxArea::Content).x);
    out.sizes.push_back(label->GetComputedValues().font_size());
    const float line = label->GetLineHeight();
    if (line > 0.0F) {
      out.most_lines =
          std::max(out.most_lines,
                   label->GetBox().GetSize(Rml::BoxArea::Content).y / line);
    }
  }
  return out;
}

Measured measure(Rml::ElementDocument *document,
                 const std::vector<std::string> &upper) {
  return measure(document, "tm-tab", upper);
}

} // namespace

int main() {
  NullRender render;
  Rml::SetRenderInterface(&render);
  Rml::Initialise();
  Rml::LoadFontFace(X2_UI_FONT_PATH);
  Rml::LoadFontFace(X2_UI_FONT_BOLD_PATH);
  Rml::Context *context =
      Rml::CreateContext("tabs", Rml::Vector2i(1280, 720), &render);
  const std::string base = std::string(X2_UI_RESOURCE_DIR) + "/touch_menu.rml";
  const std::vector<std::string> labels = {"screens", "cinematics", "stats"};
  const std::vector<std::string> upper = {"SCREENS", "CINEMATICS", "STATS"};

  Rml::ElementDocument *narrow =
      context->LoadDocumentFromMemory(tab_bar(190.0F), base);
  check(narrow != nullptr, "the tab bar loads");
  if (narrow == nullptr) {
    return 1;
  }
  const Measured before = measure(narrow, upper);
  check(before.widest_overflow > 0.0F,
        "CINEMATICS at the menu's type is wider than a narrow tab");
  x2::ui::fit_button_labels(narrow, "tm-tab", labels);
  const Measured after = measure(narrow, upper);
  check(after.widest_overflow <= 0.0F,
        "after fitting every label is inside its tab's padding");
  check(after.sizes.size() == 3u && after.sizes[0] == after.sizes[1] &&
            after.sizes[1] == after.sizes[2] &&
            after.sizes[0] < before.sizes[0],
        "every tab shrinks by the same factor");

  Rml::ElementDocument *wide =
      context->LoadDocumentFromMemory(tab_bar(400.0F), base);
  const Measured roomy = measure(wide, upper);
  x2::ui::fit_button_labels(wide, "tm-tab", labels);
  check(measure(wide, upper).sizes == roomy.sizes,
        "labels that fit keep the stylesheet's size");

  /* The skills tab's six footers at 1280x720 as layout_touch_menu places
     them: 150 px each under the document's 26 px root. */
  const std::vector<std::string> footers = {"Add",    "Auto",   "Details",
                                            "Accept", "Assign", "Next hero"};
  const std::vector<std::string> footers_upper = {
      "ADD", "AUTO", "DETAILS", "ACCEPT", "ASSIGN", "NEXT HERO"};
  Rml::ElementDocument *row = context->LoadDocumentFromMemory(
      button_row("tm-footer", footers, 150.0F, 26), base);
  const Measured crowded = measure(row, "tm-footer", footers_upper);
  check(crowded.widest_overflow > 0.0F,
        "NEXT HERO at the menu's type is wider than its footer");
  x2::ui::fit_button_labels(row, "tm-footer", footers);
  const Measured fitted = measure(row, "tm-footer", footers_upper);
  check(fitted.widest_overflow <= 0.0F && fitted.most_lines < 1.5F,
        "after fitting every footer label is on one line inside its button");
  check(fitted.sizes.size() == 6u && fitted.sizes[0] == fitted.sizes[5] &&
            fitted.sizes[0] < crowded.sizes[0],
        "every footer shrinks by the same factor");

  Rml::Shutdown();
  if (failures != 0) {
    std::printf("touch_label_fit: %d of %d check(s) failed\n", failures,
                checks);
    return 1;
  }
  std::printf("touch_label_fit: %d check(s) passed\n", checks);
  return 0;
}
