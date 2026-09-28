#ifndef OPENOS_MENU_H
#define OPENOS_MENU_H

#include "apps.h"

// The app menu: a full-screen overlay with a grid of rounded tiles (one
// per app), Wii-menu style, now with PAGES: page 0 is the built-in apps,
// page 1 is downloaded software (manifests in /apps/*.app) with empty
// slots waiting for future installs. The desktop owns the input loop and
// calls these; menu_show launches nothing itself -- the desktop does.

void menu_init(void (*repaint_cb)(void));
void menu_show(void);
void menu_hide(void);
int  menu_open(void);                    // is the overlay up?
void menu_paint(void);
void menu_hover(int mx, int my);         // highlight the tile under the mouse
enum app_id menu_pick(int mx, int my);   // APP_COUNT = no tile hit
void menu_move(int delta);               // arrows: -1 left/up, +1 right/down
enum app_id menu_selected(void);         // the highlighted tile
const char *menu_target(void);           // launch target of the last
                                         // pick/selected ("app:Code",
                                         // "file:/apps/x.js", "" = builtin)

#endif
