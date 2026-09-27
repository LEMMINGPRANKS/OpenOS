#ifndef OPENOS_INTERNET_H
#define OPENOS_INTERNET_H

#include "term.h"

// The Internet app: browser + news + updates + comments + ideas in one
// window, with real DNS (names, not just IP addresses).

void internet_app_open(struct console *con);
void internet_app_input(struct console *con, char c);
void internet_app_click(struct console *con, int mx, int my);
void internet_app_repaint(struct console *con);   // after full cell renders

#endif
