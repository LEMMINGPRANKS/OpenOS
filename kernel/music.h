#ifndef OPENOS_MUSIC_H
#define OPENOS_MUSIC_H

#include "term.h"

// The Music app: plays tunes through the PC speaker (i8254 channel 2,
// gated through port 0x61) and doubles as a free-play keyboard.

void music_app_open(struct console *con);
void music_app_input(struct console *con, char c);
void music_app_click(struct console *con, int mx, int my);
void music_app_repaint(struct console *con);    // after full cell renders

#endif
