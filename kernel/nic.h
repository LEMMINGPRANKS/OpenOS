#ifndef OPENOS_NIC_H
#define OPENOS_NIC_H

#include <stdint.h>

// Network card layer: probes every driver in turn and routes frames to
// whichever card was found. net.c talks to this, never to a driver.
//
// Drivers: Intel e1000/e1000e family, Realtek RTL8169/8168/8111/8101,
// Realtek RTL8139. All poll -- no interrupts.

#define NIC_MTU     1536                // frame buffer size everywhere
#define NIC_MIN_FRAME 60                // shorter frames get zero-padded

int  nic_init(void);                    // 0 = a card is up
int  nic_up(void);
void nic_mac(uint8_t *six_bytes);
const char *nic_name(void);             // "Intel 82540EM", ... or "none"
// frame buffer must have room for NIC_MIN_FRAME bytes (padding is in place)
int  nic_send(uint8_t *frame, uint32_t len);   // 0 = ok
int  nic_recv(uint8_t *buf, uint32_t max);     // len, 0 = nothing

#endif
