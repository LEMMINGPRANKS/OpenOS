#ifndef OPENOS_BROWSER_H
#define OPENOS_BROWSER_H

#include <stdint.h>
#include "term.h"

// The v1.0.2 browser demo: renders an HTML page into a console as
// tag-aware text (headings coloured, links shown, scripts hidden).
// Input is (buffer,len) so the News app can feed it network data.
// url != 0 draws an "open: <url>" address line above the page.

void browser_render(struct console *con, const char *html, uint32_t len,
                    const char *url);

// clickable links: pixel -> href of the link under (mx,my), if any
int browser_link_at(struct console *con, int mx, int my, char *href, int hmax);

#endif
