#ifndef X2_CONTROLLER_ASSIGNMENT_ROWS_HPP
#define X2_CONTROLLER_ASSIGNMENT_ROWS_HPP

#include <string>
#include <vector>

extern "C" {
#include "settings.h"
}

namespace x2::ui {

struct ControllerAssignmentRow {
    std::string id;
    std::string name;
    bool stable_identity;
    bool transient_assignment;
    int pad;
    int owner;
};

std::vector<ControllerAssignmentRow>
controller_assignment_rows(const X2Settings& settings);

} // namespace x2::ui

#endif
