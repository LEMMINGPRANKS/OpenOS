#ifndef OPENOS_BANNER_H
#define OPENOS_BANNER_H

#include "apps.h"

// Wii-channel-style start banner: plays a short animated splash when an
// app launches, then the window appears. wm_open calls this.

void banner_play(enum app_id app);

#endif
