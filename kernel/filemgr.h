#ifndef OPENOS_FILEMGR_H
#define OPENOS_FILEMGR_H

#include "term.h"

// The Files app: a keyboard + mouse file browser over IR2 + ramfs.
// Arrows/Enter navigate; click selects, double-click opens.

void filemgr_open(struct console *con);
void filemgr_input(struct console *con, char c);
void filemgr_click(struct console *con, int mx, int my, int dbl);
void filemgr_app_repaint(struct console *con);   // after full cell renders

#endif
