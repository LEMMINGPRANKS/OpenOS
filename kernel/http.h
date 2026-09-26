#ifndef OPENOS_HTTP_H
#define OPENOS_HTTP_H

#include <stdint.h>

// Shared HTTP client (extracted from getspgk so the News app and any
// future updater can use it too). One server at a time.

#define HTTP_MAX 8192

int      http_set_server(uint32_t ip, uint16_t port);
uint32_t http_server_ip(void);
uint16_t http_server_port(void);

// bring up NIC + DHCP + ARP to the server (prints progress to the
// active console); 0 = ready
int http_ensure_net(void);

// GET path from the server; returns body length, -1 network failure,
// -2 the server answered but not with 200
int http_get(const char *path, uint8_t *body, uint32_t max);

#endif
