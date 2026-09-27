#ifndef OPENOS_APPBAR_H
#define OPENOS_APPBAR_H

#include <stdint.h>
#include "term.h"

// The pixel toolbar every app wears under its title bar: light band,
// accent dot + title on the left, status text on the right. Paint it in
// rows term_protect reserved and nothing the app prints can wipe it.

#define APPBAR_ROWS 3                      // console rows the band covers

void appbar_paint(struct console *con, const char *title,
                  const char *status, uint32_t accent);

#endif
