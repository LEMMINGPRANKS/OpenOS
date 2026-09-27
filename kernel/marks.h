#ifndef OPENOS_MARKS_H
#define OPENOS_MARKS_H

#include <stdint.h>

// Bookmarks + browser history. Both live as plain text files in ramfs
// (/bookmarks.txt, /history.txt) and are flushed to DR1, so they survive
// a power-off like everything else. marks_home_page builds the Browser's
// local home page (bookmarks + recent history as clickable links).

void marks_bookmark(const char *url);       // remember a page (no duplicates)
void marks_history_add(const char *url);    // note a visited page (newest first)

// builds the home page HTML into buf; returns its length
int  marks_home_page(char *buf, int max);

#endif
