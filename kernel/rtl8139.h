#ifndef OPENOS_RTL8139_H
#define OPENOS_RTL8139_H

#include <stdint.h>

// Realtek RTL8139 10/100 NIC driver (QEMU -nic model=rtl8139, older PCs),
// polling mode, IO-port registers. Used through nic.c.

int  rtl8139_init(void);
void rtl8139_mac(uint8_t *six_bytes);
int  rtl8139_send(const uint8_t *frame, uint32_t len);
int  rtl8139_recv(uint8_t *buf, uint32_t max);
const char *rtl8139_model(void);

#endif
