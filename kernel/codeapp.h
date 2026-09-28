#ifndef OPENOS_CODEAPP_H
#define OPENOS_CODEAPP_H

#include "term.h"

// The Code app: a code editor with live syntax colouring for C++, C
// headers, JavaScript and HTML. Files live in ramfs + DR1 like Notepad,
// and it opens .cpp/.h/.hpp files from the Files app.

void code_open(struct console *con);
void code_input(struct console *con, char c);

#endif
