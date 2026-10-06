#ifndef X2_UI_TEXT_SCALE_H
#define X2_UI_TEXT_SCALE_H

/* The factor every glyph the engine loads is scaled by. `ui.text_scale` in
   x2native.conf, X2_TEXT_SCALE in the environment, or AUTO (0) -- which holds
   the share of the screen the text has at 800x600. See ui_text_scale.c for
   the measurement this rests on. */
float x2_ui_text_scale(void);

/* Re-derive every font already in memory at the current output resolution,
   returning how many were rewritten. A live resolution change does not reload
   fonts, so without this the text keeps the size the BOOT resolution asked
   for. Nothing to do (and 0 returned) when the scale has not moved. */
int x2_ui_text_scale_reapply(void);

/* One line at shutdown: how many glyphs were scaled and how many were not.
   A run where the override never fired must not read like a run where it
   fired and changed nothing. */
void x2_ui_text_scale_report(void);

#endif /* X2_UI_TEXT_SCALE_H */
