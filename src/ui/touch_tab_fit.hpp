#ifndef X2_TOUCH_TAB_FIT_HPP
#define X2_TOUCH_TAB_FIT_HPP

#include <string>
#include <vector>

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace x2::ui {

/* Shrinks every touch menu tab label (each `.tm-tab`'s first child) by one
   factor so the widest fits inside its tab's content box. `labels` are the
   tabs' texts in document order; they are measured uppercased, as the
   stylesheet draws them. */
void fit_tab_labels(Rml::ElementDocument *document,
                    const std::vector<std::string> &labels);

} // namespace x2::ui

#endif
