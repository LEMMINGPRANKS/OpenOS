#ifndef OPENOS_E1000_H
#define OPENOS_E1000_H

#include <stdint.h>

// Intel e1000 / e1000e NIC driver (QEMU's default network card, plus
// the 8254x/8257x/8258x chips and the I217/I218/I219 on-board LOMs),
// polling mode, legacy descriptors. Used through nic.c.

int  e1000_init(void);                 // find card on PCI, set up rings
void e1000_mac(uint8_t *six_bytes);
int  e1000_send(const uint8_t *frame, uint32_t len);   // 0 = ok
int  e1000_recv(uint8_t *buf, uint32_t max);           // len, 0 = nothing
const char *e1000_model(void);

#endif
