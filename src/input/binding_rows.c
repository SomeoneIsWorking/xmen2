/*
 * One row catalogue for XMen2.exe's input bindings.
 *
 * `storage_key` is read out of FUN_0061b030 and is part of the registry ABI;
 * even the shipped "SreenGrab" typo is therefore immutable. `display_label`
 * is the corresponding English text from the shipped PC igct.bnx. Keeping the
 * two meanings in one descriptor prevents a presentation layer from exposing
 * persistence identifiers such as Ally and TargetLock to players.
 */
#include "binding_rows.h"

#include <stddef.h>

typedef struct {
    const char *storage_key;
    const char *display_label;
} BindingRow;

static const BindingRow ROWS[INPUT_BINDING_ROWS] = {
    {"Forward", "Move Forward"},
    {"Backward", "Move Backward"},
    {"MoveLeft", "Move Left"},
    {"MoveRight", "Move Right"},
    {"LowAttack", "Attack / Power 1"},
    {"HighAttack", "Smash / Power 2"},
    {"Jump", "Jump / Xtreme"},
    {"Guard", "Use / Boost"},
    {"Power", "Use Powers"},
    {"Ally", "Energy Pack"},
    {"TargetLock", "Health Pack"},
    {"Solo", "Call Allies"},
    {"NextHero", "Chr.Up"},
    {"PreviousHero", "Chr. Down"},
    {"DecreaseHeroAggr", "Chr. Left"},
    {"IncreaseHeroAggr", "Chr. Right"},
    {"MapToggle", "Map Toggle"},
    {"Pause", "Start / Pause"},
    {"Stats", "Stats Menu"},
    {"CameraUp", "Camera Up"},
    {"CameraDown", "Camera Down"},
    {"CameraLeft", "Camera Left"},
    {"CameraRight", "Camera Right"},
    {"SreenGrab", "Screenshot"},
    {"Talk", "Talk"},
    {"Walk", "Walk"},
    {"SwtHero", "Switch Chr."},
    {"AttackObject", "Attack Obj."},
    {"RotateCamera", "Rotate Cam."},
    {"BindPower", "Bind Power"},
    {"UseQuickPower", "Quick Power"},
    {"QuickPower01", "Power 1"},
    {"QuickPower02", "Power 2"},
    {"QuickPower03", "Power 3"},
    {"QuickPower04", "Power 4"},
    {"QuickPower05", "Power 5"},
    {"QuickPower06", "Power 6"},
    {"QuickPower07", "Power 7"},
    {"QuickPower08", "Power 8"},
    {"QuickPower09", "Power 9"},
    {"QuickPower10", "Power 10"},
    {"QuickPower11", "Power 11"},
};

_Static_assert(sizeof ROWS / sizeof ROWS[0] == INPUT_BINDING_ROWS,
               "binding-row catalogue must describe every guest row");

const char *input_binding_row_storage_key(uint32_t row)
{
    return row < INPUT_BINDING_ROWS ? ROWS[row].storage_key : NULL;
}

const char *input_binding_row_display_label(uint32_t row)
{
    return row < INPUT_BINDING_ROWS ? ROWS[row].display_label : NULL;
}
