#ifndef OPENOS_E1000_H
#define OPENOS_E1000_H

#include <stdint.h>

// Intel e1000 NIC driver (QEMU's default network card), polling mode.
// One frame at a time in, one frame at a time out. No interrupts.

#define E1000_MTU 1536

int  e1000_init(void);                 // find card on PCI, set up rings
int  e1000_up(void);
void e1000_mac(uint8_t *six_bytes);
int  e1000_send(const uint8_t *frame, uint32_t len);   // 0 = ok
int  e1000_recv(uint8_t *buf, uint32_t max);           // len, 0 = nothing

#endif
