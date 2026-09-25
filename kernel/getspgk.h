#ifndef OPENOS_GETSPGK_H
#define OPENOS_GETSPGK_H

#include <stdint.h>

// getspgk: OpenOS's downloader (curl + apt in one). Downloads packages
// from the spgk server (via the gateway at 10.0.2.2:8080) into ramfs.

struct spgk_arg { const char *arg; };

void cmd_getspgk(const char *arg);       // arg = "" | list | install <pkg>
void cmd_netinfo(void);

#endif
