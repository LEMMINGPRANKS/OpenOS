#ifndef OPENOS_UPDAPP_H
#define OPENOS_UPDAPP_H

#include "term.h"

// The Update app: check + install kernel updates into the A/B slots.

void update_app_open(struct console *con);
void update_app_input(struct console *con, char c);
void update_app_repaint(struct console *con);     // after full cell renders

#endif
