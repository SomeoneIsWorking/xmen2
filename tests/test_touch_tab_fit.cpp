/* fit_tab_labels on a real RmlUi layout with the shipped stylesheet and font.
 */
#include "touch_tab_fit.hpp"

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

std::string tab_bar(float tab_width) {
  const char *labels[] = {"screens", "cinematics", "stats"};
  std::string body;
  for (int i = 0; i < 3; ++i) {
    body += "<div class='tm-button tm-tab' style='left:" +
            std::to_string(static_cast<float>(i) * tab_width) +
            "px;top:0px;width:" + std::to_string(tab_width) +
            "px;height:60px;'><span class='tm-label'>" + labels[i] +
            "</span></div>";
  }
  return "<rml><head><link type='text/rcss' href='touch_menu.rcss' /></head>"
         "<body style='font-size:30px;'>" +
         body + "</body></rml>";
}

struct Measured {
  float widest_overflow = 0.0F;
  std::vector<float> sizes;
};

Measured measure(Rml::ElementDocument *document,
                 const std::vector<std::string> &upper) {
  document->UpdateDocument();
  Rml::ElementList tabs;
  document->GetElementsByClassName(tabs, "tm-tab");
  Measured out;
  for (std::size_t i = 0; i < tabs.size(); ++i) {
    Rml::Element *label = tabs[i]->GetFirstChild();
    const auto width = static_cast<float>(
        Rml::ElementUtilities::GetStringWidth(label, upper[i]));
    out.widest_overflow =
        std::max(out.widest_overflow,
                 width - tabs[i]->GetBox().GetSize(Rml::BoxArea::Content).x);
    out.sizes.push_back(label->GetComputedValues().font_size());
  }
  return out;
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
  x2::ui::fit_tab_labels(narrow, labels);
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
  x2::ui::fit_tab_labels(wide, labels);
  check(measure(wide, upper).sizes == roomy.sizes,
        "labels that fit keep the stylesheet's size");

  Rml::Shutdown();
  if (failures != 0) {
    std::printf("touch_tab_fit: %d of %d check(s) failed\n", failures, checks);
    return 1;
  }
  std::printf("touch_tab_fit: %d check(s) passed\n", checks);
  return 0;
}
