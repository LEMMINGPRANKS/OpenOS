#ifndef OPENOS_NEWS_H
#define OPENOS_NEWS_H

#include "term.h"

// The News app: fetches the software-updates page from the spgk
// server over our own TCP stack and renders it as HTML.

void news_fetch(struct console *con);

#endif
