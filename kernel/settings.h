#ifndef OPENOS_SETTINGS_H
#define OPENOS_SETTINGS_H

#include "term.h"

// System settings, stored as key=value lines in /settings.txt (ramfs,
// flushed to DR1 -- survives reboot). Plus the Settings app window.

int  settings_get(const char *key, char *out, int max);   // 0 = found
void settings_set(const char *key, const char *value);    // saves to DR1

void settings_app_open(struct console *con);
void settings_app_input(struct console *con, char c);

#endif
