#ifndef OPENOS_MARKS_H
#define OPENOS_MARKS_H

#include <stdint.h>

// Bookmarks + browser history. Both live as plain text files in ramfs
// (/bookmarks.txt, /history.txt) and are flushed to DR1, so they survive
// a power-off like everything else.

void marks_bookmark(const char *url);       // remember a page (no duplicates)
void marks_history_add(const char *url);    // note a visited page (newest first)

// copy up to max bookmark urls into out[96]s, newest first; returns count
int  marks_bookmarks(char out[][96], int max);

#endif
