/*
 * menu_dialog.h -- a dialog drawn by the game: a title, text and up to four buttons, on the menus'
 * backdrop with the menus' own font and buttons (src/menu_dialog.c,
 * bin/data_json/menus/openswos/menuDialog.json). SDL builds. In place of the OS's message boxes
 * wherever the menus are up: the game data's and the world's notices at start-up
 * (src/assets_check.c), OPTIONS' notes; a mod's through ModApi_ShowDialog.
 */
#ifndef MENU_DIALOG_H
#define MENU_DIALOG_H

#include <stdint.h>

enum { MENU_DIALOG_MAX_BUTTONS = 4 };

/* Shows the dialog until a button is chosen: its index (0..count-1), or -1 for ESC.
 * `text`: any case (shown in capitals: the menu font has no small letters), wrapped to the
 * screen; '|' or '\n' starts a new paragraph. Buttons stack at the bottom, in order; the
 * cursor starts on `defaultButton`. */
int32_t MenuDialog_Show(const char *title, const char *text, const char *const *buttons, int32_t count,
                        int32_t defaultButton);

/* A long text to read, a page at a time (a mod's readme): the title and the page's number in
 * the header, the text left-aligned and wrapped, PREVIOUS / EXIT / NEXT under it (EXIT the
 * menus' brown, in the centre). `text` is as written: CR LF or LF lines, UTF-8 or Latin-1;
 * characters the menu font has no glyph for are shown as spaces. Returns on EXIT or ESC. */
void MenuDialog_ShowText(const char *title, const char *text);

#endif /* MENU_DIALOG_H */
