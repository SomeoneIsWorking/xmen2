/*
 * Canonical Xbox-release controller bindings.
 *
 * The Xbox action constructor at 0x00162240 and its authored controller screen
 * supply the semantics. XMen2.exe FUN_00619c40 supplies the corresponding PC
 * rows, and FUN_006281f0 supplies the DirectInput physical codes. This module
 * owns only that evidence-derived table. Device ownership, slot selection and
 * publication into master/working/menu sets belong to src/input/player_input.c.
 *
 * Black/White remain outside the table: the Xbox constructor registers neither
 * as a common action and the PC binding object has no Health/Energy row. Their
 * direct gameplay path is a separate RE boundary, not an alias invented here.
 */
#include "xbox_defaults.h"

/* Axis pairs are positive then negative: LX 1/2, LY 3/4, Rx 7/8, Ry 9/10.
   POV is X+/X-/Y+/Y- at 0x11..0x14; buttons start at 0x15 in A/B/X/Y order. */
static const XboxDefaultBinding DEFAULTS[] = {
    {  0, 0x04 }, /* Forward       LY- */
    {  1, 0x03 }, /* Backward      LY+ */
    {  2, 0x02 }, /* MoveLeft      LX- */
    {  3, 0x01 }, /* MoveRight     LX+ */
    {  4, 0x15 }, /* LowAttack     A / Punch */
    {  5, 0x16 }, /* HighAttack    B / Slam */
    {  6, 0x18 }, /* Jump          Y / Jump + Xtreme */
    {  7, 0x17 }, /* Guard         X / Use + Pickup + Boost */
    {  8, 0x06 }, /* Power         RT */
    {  9, 0x05 }, /* Ally          LT */
    { 12, 0x14 }, /* NextHero      d-pad up */
    { 13, 0x13 }, /* PreviousHero  d-pad down */
    { 14, 0x12 }, /* DecreaseAggr  d-pad left */
    { 15, 0x11 }, /* IncreaseAggr  d-pad right */
    { 16, 0x1e }, /* MapToggle     right-stick click */
    { 17, 0x1c }, /* Pause         Start */
    { 18, 0x1b }, /* Stats         Back / Team Information */
    { 19, 0x0a }, /* CameraUp      Ry- */
    { 20, 0x09 }, /* CameraDown    Ry+ */
    { 21, 0x08 }, /* CameraLeft    Rx- */
    { 22, 0x07 }, /* CameraRight   Rx+ */
};

const XboxDefaultBinding *xbox_default_bindings(size_t *count)
{
    if (count) *count = sizeof DEFAULTS / sizeof DEFAULTS[0];
    return DEFAULTS;
}
