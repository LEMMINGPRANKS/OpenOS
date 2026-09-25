#ifndef OPENOS_TCP_H
#define OPENOS_TCP_H

#include <stdint.h>

// Minimal TCP client: one connection at a time, no retransmit queue.
// Good enough for HTTP downloads over QEMU's reliable user-network.

int  tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms);
int  tcp_send(const uint8_t *data, uint32_t len);
int  tcp_recv(uint8_t *buf, uint32_t max, uint32_t timeout_ms); // -1 = closed
void tcp_close(void);
int  tcp_connected(void);
uint32_t tcp_remote_ip(void);

#endif
