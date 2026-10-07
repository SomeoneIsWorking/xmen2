#include "touch_menu_document.hpp"

#include "rml_text.hpp"
#include "touch_label_fit.hpp"
#include "touch_runtime_menu.hpp"
#include "ui_resources.h"

#include <RmlUi/Core.h>

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <vector>

namespace x2::ui {
namespace {

/* The body's font size in design units; the stylesheet sizes in em of it. */
constexpr float kFontUnits = 26.0F;
/* The panel's margin around the title, list and footer, in design units. */
constexpr float kPanelPad = 12.0F;

struct Scale {
  float x = 1.0F;
  float y = 1.0F;
};

std::string box(const X2Rect &rect, float origin_x, float origin_y,
                Scale scale) {
  char text[160];
  std::snprintf(text, sizeof text,
                "left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx;",
                static_cast<double>((rect.left - origin_x) * scale.x),
                static_cast<double>((rect.top - origin_y) * scale.y),
                static_cast<double>((rect.right - rect.left) * scale.x),
                static_cast<double>((rect.bottom - rect.top) * scale.y));
  return text;
}

void row_content(std::ostringstream &rml, const input::TouchMenuRow &row) {
  rml << "<span class='tm-label'>" << escape_rml(row.label) << "</span>";
  if (row.fill) {
    const float percent = std::clamp(*row.fill, 0.0F, 1.0F) * 100.0F;
    char width[32];
    std::snprintf(width, sizeof width, "%.1f%%", static_cast<double>(percent));
    rml << "<div class='tm-bar'><div class='tm-fill' style='width:" << width
        << ";'></div></div>";
  } else if (!row.value.empty()) {
    rml << "<span class='tm-value'>" << escape_rml(row.value) << "</span>";
  }
}

void detail_content(std::ostringstream &rml, const input::TouchMenuState &state,
                    Scale scale) {
  const input::TouchMenuLayout &layout = state.layout;
  if (layout.detail.bottom <= layout.detail.top) {
    return;
  }
  rml << "<div id='tm-detail' style='" << box(layout.detail, 0.0F, 0.0F, scale)
      << "'>";
  if (!state.view.facts.empty()) {
    rml << "<div class='tm-facts'>";
    for (const input::TouchMenuFact &fact : state.view.facts) {
      rml << "<span class='tm-fact'>";
      if (!fact.label.empty()) {
        rml << "<span class='tm-fact-label'>" << escape_rml(fact.label)
            << "</span>";
      }
      rml << "<span class='tm-fact-value" << (fact.warn ? " warn" : "") << "'>"
          << escape_rml(fact.value) << "</span></span>";
    }
    rml << "</div>";
  }
  rml << "</div>";
  /* Whole lines only: the backend does not clip, and read text scrolls. */
  const float height = layout.detail_line_height;
  for (std::size_t i = 0; i < state.view.detail.size(); ++i) {
    const float line_top =
        layout.detail_text_top + static_cast<float>(i) * height;
    if (line_top < layout.detail_text.top ||
        line_top + height > layout.detail_text.bottom) {
      continue;
    }
    const X2Rect line{layout.detail.left, line_top, layout.detail.right,
                      line_top + height};
    rml << "<div class='tm-text' style='" << box(line, 0.0F, 0.0F, scale)
        << "'>" << escape_rml(state.view.detail[i]) << "</div>";
  }
}

std::string markup(const input::TouchMenuState &state, Scale scale) {
  const input::TouchMenuLayout &layout = state.layout;
  const float pad = kPanelPad * layout.unit;
  const X2Rect panel{layout.title.left - pad, layout.title.top - pad,
                     layout.footer.right + pad, layout.footer.bottom + pad};
  /* Rows scrolled past the list are covered rather than clipped: the SDL_GPU
     backend does not clip overflow. */
  const X2Rect above{layout.list.left, panel.top + 0.5F * pad,
                     layout.list.right, layout.list.top};
  const X2Rect below{layout.list.left, layout.list.bottom, layout.list.right,
                     panel.bottom - 0.5F * pad};
  std::ostringstream rml;
  rml << "<div id='tm-panel' style='" << box(panel, 0.0F, 0.0F, scale)
      << "'></div>";
  rml << "<div id='tm-list' style='" << box(layout.list, 0.0F, 0.0F, scale)
      << "'>";
  std::ostringstream footer;
  std::ostringstream pinned;
  for (std::size_t i = 0; i < layout.buttons.size(); ++i) {
    const input::TouchMenuButton &button = layout.buttons[i];
    const bool pressed = state.pressed == static_cast<int>(i);
    if (button.part == input::TouchMenuPart::tab) {
      const input::TouchMenuTab &tab =
          state.view.tabs[static_cast<std::size_t>(button.index)];
      pinned << "<div class='tm-button tm-tab" << (tab.lit ? " lit" : "")
             << (pressed ? " pressed" : "") << "' style='"
             << box(button.rect, 0.0F, 0.0F, scale)
             << "'><span class='tm-label'>" << escape_rml(tab.label)
             << "</span></div>";
      continue;
    }
    if (button.part == input::TouchMenuPart::footer) {
      const input::TouchMenuFooter &action =
          state.view.footers[static_cast<std::size_t>(button.index)];
      footer << "<div class='tm-button tm-footer" << (pressed ? " pressed" : "")
             << "' style='" << box(button.rect, 0.0F, 0.0F, scale)
             << "'><span class='tm-label'>" << escape_rml(action.label)
             << "</span></div>";
      continue;
    }
    if (button.rect.bottom <= layout.list.top ||
        button.rect.top >= layout.list.bottom) {
      continue;
    }
    const input::TouchMenuRow &row =
        state.view.rows[static_cast<std::size_t>(button.index)];
    rml << "<div class='tm-button"
        << (button.part == input::TouchMenuPart::row ? "" : " tm-step")
        << (row.focused && button.part == input::TouchMenuPart::row ? " focused"
                                                                    : "")
        << (pressed ? " pressed" : "") << "' style='"
        << box(button.rect, layout.list.left, layout.list.top, scale) << "'>";
    if (button.part == input::TouchMenuPart::row) {
      row_content(rml, row);
    } else {
      rml << "<span class='tm-glyph'>"
          << (button.part == input::TouchMenuPart::step_left ? "&#8249;"
                                                             : "&#8250;")
          << "</span>";
    }
    rml << "</div>";
  }
  rml << "</div>";
  rml << "<div class='tm-cover' style='" << box(above, 0.0F, 0.0F, scale)
      << "'></div><div class='tm-cover' style='"
      << box(below, 0.0F, 0.0F, scale) << "'></div>";
  if (!state.view.title.empty()) {
    rml << "<div id='tm-title' style='" << box(layout.title, 0.0F, 0.0F, scale)
        << "'><span>" << escape_rml(state.view.title) << "</span></div>";
  }
  rml << pinned.str();
  detail_content(rml, state, scale);
  rml << footer.str();
  return rml.str();
}

} // namespace

bool TouchMenuDocument::load(Rml::Context *context) {
  const std::string base = x2_ui_resource_path("touch_menu.rml");
  const std::string shell =
      "<rml><head><title>Touch Menu</title><link type='text/rcss' "
      "href='touch_menu.rcss' /></head><body id='tm-root'></body></rml>";
  document_ = context->LoadDocumentFromMemory(shell, base);
  if (!document_) {
    return false;
  }
  root_ = document_->GetElementById("tm-root");
  if (!root_) {
    return false;
  }
  document_->Hide();
  visible_ = false;
  return true;
}

void TouchMenuDocument::shutdown() {
  document_ = nullptr;
  root_ = nullptr;
  drawn_.clear();
  visible_ = false;
}

bool TouchMenuDocument::wanted() { return input::touch_menu_state().shown; }

void TouchMenuDocument::update() {
  if (!document_ || !root_) {
    return;
  }
  const input::TouchMenuState state = input::touch_menu_state();
  const Rml::Vector2i dimensions = document_->GetContext()->GetDimensions();
  const bool drawable =
      allowed_ && state.shown && state.viewport.width > 0.0F &&
      state.viewport.height > 0.0F && dimensions.x > 0 && dimensions.y > 0;
  if (drawable != visible_) {
    visible_ = drawable;
    if (drawable) {
      document_->Show();
    } else {
      document_->Hide();
      drawn_.clear();
    }
  }
  if (!drawable) {
    return;
  }
  const Scale scale{static_cast<float>(dimensions.x) / state.viewport.width,
                    static_cast<float>(dimensions.y) / state.viewport.height};
  root_->SetProperty(
      Rml::PropertyId::FontSize,
      Rml::Property(kFontUnits * state.layout.unit * scale.y, Rml::Unit::PX));
  std::string next = markup(state, scale);
  if (next != drawn_) {
    root_->SetInnerRML(next);
    std::vector<std::string> tabs;
    std::vector<std::string> footers;
    for (const input::TouchMenuButton &button : state.layout.buttons) {
      const auto index = static_cast<std::size_t>(button.index);
      if (button.part == input::TouchMenuPart::tab) {
        tabs.push_back(state.view.tabs[index].label);
      } else if (button.part == input::TouchMenuPart::footer) {
        footers.push_back(state.view.footers[index].label);
      }
    }
    fit_button_labels(document_, "tm-tab", tabs);
    fit_button_labels(document_, "tm-footer", footers);
    drawn_ = std::move(next);
  }
}

} // namespace x2::ui
