/* The menu text tokens the retail UI resolves, and what each becomes. */
#ifndef X2_PROMPT_TOKENS_H
#define X2_PROMPT_TOKENS_H

/*
 * XMen2.exe FUN_004bd720 is the token resolver the menus' authored text runs
 * through: `UI/menus/game_options.engb` holds the literal string
 * "$MENU_BACK Back", and this is what turns `$MENU_BACK` into the words drawn
 * in front of "Back".
 *
 * It matters to the port because only SOME tokens come back as a binding's
 * name through FUN_00619e30 -- those get the port's keycap art and can be
 * rewritten into something a finger presses. The rest resolve straight out of
 * the localization table, which is why one half of the Options footer draws
 * `[SPACE] Advanced Options` with a key cap and the other half draws a flat
 * `Esc Back` that no touch player can use.
 *
 * This owner records which is which, by token, from a real run.
 */
struct X86pCpu;
void x2_probe_004bd720(struct X86pCpu *cpu);
void x2_prompt_tokens_report(void);

#endif
