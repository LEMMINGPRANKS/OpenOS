#ifndef OPENOS_DLAPP_H
#define OPENOS_DLAPP_H

#include "term.h"

// The Download app: browse the spgk package list and install packages.

void download_app_open(struct console *con);
void download_app_input(struct console *con, char c);
void download_app_click(struct console *con, int mx, int my);
void download_app_repaint(struct console *con);   // after full cell renders

#endif
