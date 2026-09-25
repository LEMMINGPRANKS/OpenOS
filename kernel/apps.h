#ifndef OPENOS_APPS_H
#define OPENOS_APPS_H

#include "term.h"

enum app_id {
    APP_SHELL,
    APP_NOTEPAD,
    APP_FILES,
    APP_VIEWER,
    APP_COUNT
};

void app_open(enum app_id app, struct console *con);
void app_input(enum app_id app, struct console *con, char c);
const char *app_name(enum app_id app);
void app_set_arg(const char *arg);    // e.g. filename for APP_VIEWER

#endif
