#ifndef OPENOS_NET_H
#define OPENOS_NET_H

#include <stdint.h>

// The OpenOS network stack: ethernet + ARP + IPv4 + UDP, DHCP client.
// (TCP lives in tcp.c on top of this.)

#define IP(a,b,c,d) (((a)<<24)|((b)<<16)|((c)<<8)|(d))   // host-order helper

int  net_init(void);                    // NIC + DHCP (+ DNS server from DHCP)
int  net_up(void);                      // card found AND DHCP gave us an IP
uint32_t net_local_ip(void);            // host order
void net_ip_str(uint32_t ip, char *out);// "10.0.2.15"
uint32_t net_gateway(void);             // DHCP router, 0 = none
uint32_t net_dns_server(void);          // DHCP DNS server, 0 = none

// UDP: send a datagram; net_udp_poll waits for one matching port (ms timeout)
int  net_udp_send(const uint8_t *data, uint32_t len,
                  uint32_t dst_ip, uint16_t dst_port, uint16_t src_port);
int  net_udp_poll(uint8_t *out, uint32_t max, uint16_t want_src_port,
                  uint32_t want_src_ip, uint32_t timeout_ms);

// ARP
int  net_arp(uint32_t ip, uint8_t *mac_out);   // resolve (blocking, retries)

// low level: send a raw ethernet frame (TCP uses this)
int  net_send_frame(const uint8_t *dst, uint16_t ethertype,
                    const uint8_t *payload, uint32_t len);
// IP packet with a pre-built protocol header (TCP uses this)
int  net_ip_send(const uint8_t *hdr, uint32_t hdrlen,
                 const uint8_t *payload, uint32_t paylen,
                 uint32_t dst_ip, uint8_t proto);
// wait for one TCP segment (returns ip payload length; src_ip may be 0)
int  net_ip_poll_tcp(uint8_t *out, uint32_t max, uint32_t *src_ip,
                     uint32_t timeout_ms);
const uint8_t *net_our_mac(void);

#endif
