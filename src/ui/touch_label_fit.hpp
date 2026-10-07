#ifndef X2_TOUCH_LABEL_FIT_HPP
#define X2_TOUCH_LABEL_FIT_HPP

#include <string>
#include <vector>

namespace Rml {
class ElementDocument;
} // namespace Rml

namespace x2::ui {

/* Shrinks the labels of every touch menu button of class `button_class`
   (each button's first child) by one factor so the widest fits on one line
   inside its button's content box. `labels` are the buttons' texts in
   document order; they are measured uppercased, as the stylesheet draws
   them. */
void fit_button_labels(Rml::ElementDocument *document,
                       const std::string &button_class,
                       const std::vector<std::string> &labels);

} // namespace x2::ui

#endif
