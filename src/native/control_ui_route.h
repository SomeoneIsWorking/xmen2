#ifndef X2_CONTROL_UI_ROUTE_H
#define X2_CONTROL_UI_ROUTE_H

/* Press a key, or click, at the PORT's own UI layer rather than the guest's.
   See the .c: the game's input is injected into DirectInput and can never
   reach an SDL document. */
void control_ui_key_route(int fd, const char *query);
void control_ui_click_route(int fd, const char *query);

#endif
