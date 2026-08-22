#include "controller_assignment_rows.hpp"

#include <algorithm>

extern "C" {
#include "dinput_pad.h"
#include "transient_controller_assignment.h"
}

namespace x2::ui {

std::vector<ControllerAssignmentRow>
controller_assignment_rows(const X2Settings& settings)
{
    std::vector<ControllerAssignmentRow> rows;
    for (unsigned player = 0; player < X2_SETTINGS_PLAYERS; player++) {
        const char* id = x2_transient_controller_id(player);
        if (!id) continue;
        int pad = x2_transient_controller_resolve(player);
        const char* name = pad >= 0 ? dinput_pad_name(pad) : nullptr;
        rows.push_back({id, name ?
            std::string(name) + " (session assignment; not saved)" :
            "Disconnected session controller: " + std::string(id),
            false, true, pad, (int)player});
    }
    for (unsigned i = 0; i < X2_SETTINGS_CONTROLLER_ASSIGNMENTS; i++) {
        const char* id = settings.controller[i].id;
        if (!id[0]) continue;
        auto existing = std::find_if(rows.begin(), rows.end(),
            [id](const ControllerAssignmentRow& row) { return row.id == id; });
        if (existing != rows.end()) continue; /* transient overlay wins */
        int owner = settings.controller[i].player;
        if (owner >= 0 && x2_transient_controller_has_assignment(owner))
            owner = -2;
        rows.push_back({id, "Disconnected: " + std::string(id), true, false,
                        -1, owner});
    }
    for (int pad = 0; pad < DINPUT_PAD_MAX; pad++) {
        const char* id = dinput_pad_persistent_id(pad);
        const char* name = dinput_pad_name(pad);
        if (!id || !name) continue;
        auto found = std::find_if(rows.begin(), rows.end(),
            [id](const ControllerAssignmentRow& row) { return row.id == id; });
        if (found == rows.end()) {
            bool stable = dinput_pad_persistent_id_is_stable(pad);
            rows.push_back({id, stable ? name :
                std::string(name) + " (session only; not saved)", stable,
                false, pad, x2_transient_controller_player_for_pad(pad)});
        } else {
            found->name = found->transient_assignment
                ? std::string(name) + " (session assignment; not saved)"
                : name;
            found->stable_identity =
                dinput_pad_persistent_id_is_stable(pad);
            found->pad = pad;
        }
    }
    return rows;
}

} // namespace x2::ui
