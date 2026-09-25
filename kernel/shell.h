#ifndef OPENOS_SHELL_H
#define OPENOS_SHELL_H

#include "term.h"

// The shell as an app: one line-editing state per console.
// con == NULL means the boot/VGA console (no window).

void shell_execute(char *cmdline);           // run one command line
void shell_app_open(struct console *con);    // greet + prompt
void shell_app_input(struct console *con, char c);
void shell_run(void);                        // fallback loop (no desktop)

#endif
