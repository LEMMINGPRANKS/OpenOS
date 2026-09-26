#ifndef OPENOS_APPS_H
#define OPENOS_APPS_H

#include "term.h"

enum app_id {
    APP_SHELL,
    APP_NOTEPAD,
    APP_FILES,
    APP_VIEWER,
    APP_RUNNER,
    APP_COUNT
};

void app_open(enum app_id app, struct console *con);
void app_input(enum app_id app, struct console *con, char c);
void app_click(enum app_id app, struct console *con, int mx, int my, int dbl);
const char *app_name(enum app_id app);
void app_set_arg(const char *arg);    // e.g. filename for APP_VIEWER/RUNNER

// Open path (absolute) in the right app window: .js runs, anything text
// views. Returns 0 on success, -1 if no window/app matched it.
int  open_file_window(const char *path);

#endif
