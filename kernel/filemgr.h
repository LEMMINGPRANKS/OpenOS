#ifndef OPENOS_FILEMGR_H
#define OPENOS_FILEMGR_H

#include "term.h"

// The Files app: a keyboard-driven file browser over IR2 + ramfs.
// Up/down arrows move, Enter opens text files in a viewer window.

void filemgr_open(struct console *con);
void filemgr_input(struct console *con, char c);

#endif
