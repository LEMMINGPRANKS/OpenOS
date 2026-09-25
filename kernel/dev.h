#ifndef OPENOS_DEV_H
#define OPENOS_DEV_H

#include <stdint.h>

// OpenOS device registers (Freddie's design):
//   DR = drive register, IR = internal register, UR = unsupported register
// DR1 main drive | DR2 SD card slot | DR3 USB slot
// IR1 RAM        | IR2 initramfs
// UR1 unsupported-device trap: constantly polled; fires panic when tripped

void dev_list(void);
void ur1_poll(void);
int dev_fire(const char *name);

#endif
