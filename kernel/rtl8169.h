#ifndef OPENOS_RTL8169_H
#define OPENOS_RTL8169_H

#include <stdint.h>

// Realtek RTL8169 / RTL8168 / RTL8111 / RTL8101 NIC driver -- the on-board
// Ethernet of most desktop and laptop boards. Polling mode, IO-port
// registers, descriptor rings. Used through nic.c.

int  rtl8169_init(void);
void rtl8169_mac(uint8_t *six_bytes);
int  rtl8169_send(const uint8_t *frame, uint32_t len);
int  rtl8169_recv(uint8_t *buf, uint32_t max);
const char *rtl8169_model(void);

#endif
