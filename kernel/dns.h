#ifndef OPENOS_DNS_H
#define OPENOS_DNS_H

#include <stdint.h>

// DNS resolver (A records) over the UDP stack. Names in, IPv4 out
// (host order, same convention as net.h). 0 = could not resolve.

void     dns_set_server(uint32_t ip);
uint32_t dns_server(void);
uint32_t dns_resolve(const char *name);   // "example.com" -> IP, 0 on fail

#endif
