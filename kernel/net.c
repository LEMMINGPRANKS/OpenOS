#include "net.h"
#include "e1000.h"
#include "timer.h"
#include "term.h"

#define ETH_ARP 0x0806
#define ETH_IP  0x0800

struct eth_hdr {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t type;
} __attribute__((packed));

struct arp_pkt {
    uint16_t htype, ptype;
    uint8_t  hlen, plen;
    uint16_t oper;                      // 1 = request, 2 = reply
    uint8_t  sha[6];
    uint32_t spa;
    uint8_t  tha[6];
    uint32_t tpa;
} __attribute__((packed));

struct ip_hdr {
    uint8_t  verihl, tos;
    uint16_t total, id, frag;
    uint8_t  ttl, proto;
    uint16_t csum;
    uint32_t src, dst;
} __attribute__((packed));

struct udp_hdr {
    uint16_t sport, dport, len, csum;
} __attribute__((packed));

struct dhcp_pkt {
    uint8_t  op, htype, hlen, hops;
    uint32_t xid;
    uint16_t secs, flags;
    uint32_t ciaddr, yiaddr, siaddr, giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  opts[64];
} __attribute__((packed));

static uint8_t our_mac[6];
static uint8_t gw_mac[6];
static int      gw_mac_known;
static uint32_t our_ip;                 // host order

static uint8_t txf[E1000_MTU] __attribute__((aligned(8)));
static uint8_t rxf[E1000_MTU] __attribute__((aligned(8)));

static uint16_t htons_(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint16_t ntohs_(uint16_t v) { return htons_(v); }
static uint32_t htonl_(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | (v >> 24);
}

static uint16_t csum16(const uint8_t *p, uint32_t n)
{
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < n; i += 2)
        sum += (uint32_t)p[i] << 8 | p[i + 1];
    if (n & 1)
        sum += (uint32_t)p[n - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

const uint8_t *net_our_mac(void) { return our_mac; }
void net_gw_mac(uint8_t *out)
{
    for (int i = 0; i < 6; i++) out[i] = gw_mac[i];
}
int  net_up(void)      { return e1000_up(); }
uint32_t net_local_ip(void) { return our_ip; }

void net_ip_str(uint32_t ip, char *out)
{
    char digits[4];
    int n = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint32_t v = (ip >> shift) & 0xFF;
        int d = 0;
        if (!v) digits[d++] = '0';
        while (v) { digits[d++] = (char)('0' + v % 10); v /= 10; }
        while (d) out[n++] = digits[--d];
        if (shift) out[n++] = '.';
    }
    out[n] = 0;
}

// --- frame send -----------------------------------------------------------

int net_send_frame(const uint8_t *dst, uint16_t ethertype,
                   const uint8_t *payload, uint32_t len)
{
    if (len + 14 > E1000_MTU)
        return -1;
    struct eth_hdr *e = (struct eth_hdr *)txf;
    for (int i = 0; i < 6; i++) {
        e->dst[i] = dst[i];
        e->src[i] = our_mac[i];
    }
    e->type = htons_(ethertype);
    for (uint32_t i = 0; i < len; i++)
        txf[14 + i] = payload[i];
    return e1000_send(txf, len + 14);
}

// --- IP + UDP send ---------------------------------------------------------

int net_ip_send(const uint8_t *hdr, uint32_t hdrlen,
                const uint8_t *payload, uint32_t paylen,
                uint32_t dst_ip, uint8_t proto)
{
    uint32_t total = 20 + hdrlen + paylen;
    if (total + 14 > E1000_MTU)
        return -1;

    struct eth_hdr *e = (struct eth_hdr *)txf;
    if (gw_mac_known) {
        for (int i = 0; i < 6; i++) e->dst[i] = gw_mac[i];
    } else {
        for (int i = 0; i < 6; i++) e->dst[i] = 0xFF;   // broadcast
    }
    for (int i = 0; i < 6; i++) e->src[i] = our_mac[i];
    e->type = htons_(ETH_IP);

    struct ip_hdr *ip = (struct ip_hdr *)(txf + 14);
    ip->verihl = 0x45;
    ip->tos = 0;
    ip->total = htons_((uint16_t)total);
    ip->id = htons_(1);
    ip->frag = 0;
    ip->ttl = 64;
    ip->proto = proto;
    ip->csum = 0;
    ip->src = htonl_(our_ip);
    ip->dst = htonl_(dst_ip);
    ip->csum = htons_(csum16((uint8_t *)ip, 20));

    for (uint32_t i = 0; i < hdrlen; i++)
        txf[14 + 20 + i] = hdr[i];
    for (uint32_t i = 0; i < paylen; i++)
        txf[14 + 20 + hdrlen + i] = payload[i];
    return e1000_send(txf, 14 + total);
}

int net_udp_send(const uint8_t *data, uint32_t len,
                 uint32_t dst_ip, uint16_t dst_port, uint16_t src_port)
{
    struct udp_hdr u;
    u.sport = htons_(src_port);
    u.dport = htons_(dst_port);
    u.len = htons_((uint16_t)(8 + len));
    u.csum = 0;                        // legal to skip for IPv4
    return net_ip_send((uint8_t *)&u, 8, data, len, dst_ip, 17);
}

// --- receive path ----------------------------------------------------------

static void handle_arp(struct arp_pkt *a)
{
    uint32_t tpa = htonl_(a->tpa);
    if (ntohs_(a->oper) == 2) {         // reply: cache it
        if (!gw_mac_known) {
            for (int i = 0; i < 6; i++) gw_mac[i] = a->sha[i];
            gw_mac_known = 1;
        }
        return;
    }
    if (ntohs_(a->oper) == 1 && tpa == our_ip && our_ip) {
        // someone asks for us: answer
        struct arp_pkt r;
        r.htype = htons_(1);
        r.ptype = htons_(ETH_IP);
        r.hlen = 6; r.plen = 4;
        r.oper = htons_(2);
        for (int i = 0; i < 6; i++) r.sha[i] = our_mac[i];
        r.spa = htonl_(our_ip);
        for (int i = 0; i < 6; i++) r.tha[i] = a->sha[i];
        r.tpa = a->spa;
        net_send_frame(a->sha, ETH_ARP, (uint8_t *)&r, sizeof r);
    }
}

// pending UDP match, filled by net_poll
static uint8_t  udp_pending[E1000_MTU];
static uint32_t udp_pending_len;
static uint16_t udp_pending_port;
static uint32_t udp_pending_ip;

static uint8_t  tcp_pending[E1000_MTU];
static uint32_t tcp_pending_len;

static void handle_ip(struct ip_hdr *ip, uint32_t n)
{
    if ((ip->verihl >> 4) != 4)
        return;
    if (csum16((uint8_t *)ip, 20) != 0)
        return;                         // bad header checksum
    if (ip->proto == 6 && !tcp_pending_len && n >= 20) {
        // whole TCP segment (IP payload) for tcp.c
        uint32_t len = n - 20;
        if (len > E1000_MTU) len = E1000_MTU;
        uint8_t *seg = (uint8_t *)ip + 20;
        for (uint32_t i = 0; i < len; i++)
            tcp_pending[i] = seg[i];
        tcp_pending_len = len;
        return;
    }
    if (ip->proto != 17)
        return;
    uint32_t hlen = (uint32_t)(ip->verihl & 0xF) * 4;
    if (hlen + 8 > n)
        return;
    struct udp_hdr *u = (struct udp_hdr *)((uint8_t *)ip + hlen);
    if (!udp_pending_len && !udp_pending_port) {
        uint32_t ulen = ntohs_(u->len);
        if (ulen > 8) ulen -= 8; else ulen = 0;
        if (ulen > E1000_MTU) ulen = E1000_MTU;
        uint8_t *body = (uint8_t *)u + 8;
        for (uint32_t i = 0; i < ulen; i++)
            udp_pending[i] = body[i];
        udp_pending_len = ulen;
        udp_pending_port = ntohs_(u->sport);
        udp_pending_ip = htonl_(ip->src);
    }
}

static void net_poll_once(void)
{
    int n = e1000_recv(rxf, E1000_MTU);
    if (n < 14)
        return;
    struct eth_hdr *e = (struct eth_hdr *)rxf;
    uint16_t type = ntohs_(e->type);
    if (type == ETH_ARP && (uint32_t)n >= 14 + sizeof(struct arp_pkt))
        handle_arp((struct arp_pkt *)(rxf + 14));
    else if (type == ETH_IP && (uint32_t)n >= 14 + 28)
        handle_ip((struct ip_hdr *)(rxf + 14), (uint32_t)n - 14);
}

int net_ip_poll_tcp(uint8_t *out, uint32_t max, uint32_t timeout_ms)
{
    uint64_t start = timer_uptime_ms();
    for (;;) {
        tcp_pending_len = 0;
        net_poll_once();
        if (tcp_pending_len) {
            uint32_t len = tcp_pending_len > max ? max : tcp_pending_len;
            for (uint32_t i = 0; i < len; i++)
                out[i] = tcp_pending[i];
            return (int)len;
        }
        if (timer_uptime_ms() - start > timeout_ms)
            return -1;
        __asm__ volatile ("hlt");
    }
}

int net_udp_poll(uint8_t *out, uint32_t max, uint16_t want_port,
                 uint32_t want_ip, uint32_t timeout_ms)
{
    uint64_t start = timer_uptime_ms();
    for (;;) {
        udp_pending_len = 0;
        udp_pending_port = 0;
        net_poll_once();
        if (udp_pending_len &&
            (want_port == 0 || udp_pending_port == want_port) &&
            (want_ip == 0 || udp_pending_ip == want_ip)) {
            uint32_t len = udp_pending_len > max ? max : udp_pending_len;
            for (uint32_t i = 0; i < len; i++)
                out[i] = udp_pending[i];
            return (int)len;
        }
        if (timer_uptime_ms() - start > timeout_ms)
            return -1;
        __asm__ volatile ("hlt");
    }
}

// --- ARP resolve -----------------------------------------------------------

int net_arp(uint32_t ip, uint8_t *mac_out)
{
    for (int try = 0; try < 3; try++) {
        struct arp_pkt q;
        q.htype = htons_(1);
        q.ptype = htons_(ETH_IP);
        q.hlen = 6; q.plen = 4;
        q.oper = htons_(1);
        for (int i = 0; i < 6; i++) q.sha[i] = our_mac[i];
        q.spa = htonl_(our_ip);
        for (int i = 0; i < 6; i++) q.tha[i] = 0;
        q.tpa = htonl_(ip);
        uint8_t bcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
        gw_mac_known = 0;
        net_send_frame(bcast, ETH_ARP, (uint8_t *)&q, sizeof q);

        uint64_t start = timer_uptime_ms();
        while (timer_uptime_ms() - start < 500) {
            net_poll_once();
            if (gw_mac_known) {
                for (int i = 0; i < 6; i++) mac_out[i] = gw_mac[i];
                return 0;
            }
            __asm__ volatile ("hlt");
        }
    }
    return -1;
}

// --- DHCP -------------------------------------------------------------------

#define DHCP_DISCOVER 1
#define DHCP_OFFER    2
#define DHCP_REQUEST  3
#define DHCP_ACK      5

// returns option length and copies value
static int dhcp_get_option(const uint8_t *opts, uint32_t n, uint8_t want,
                           uint8_t *val, uint8_t maxv)
{
    uint32_t i = 0;
    while (i + 1 < n) {
        if (opts[i] == 255)
            return 0;
        if (opts[i] == 0) { i++; continue; }
        uint8_t len = opts[i + 1];
        if (opts[i] == want) {
            if (len > maxv) len = maxv;
            for (int k = 0; k < len; k++) val[k] = opts[i + 2 + k];
            return len;
        }
        i += 2 + len;
    }
    return 0;
}

static void dhcp_append(uint8_t *opts, int *n, uint8_t code, const uint8_t *val, uint8_t len)
{
    opts[(*n)++] = code;
    opts[(*n)++] = len;
    for (int i = 0; i < len; i++) opts[(*n)++] = val[i];
}

static int dhcp_run(void)
{
    static struct dhcp_pkt d;           // ~600 bytes, fine on stack? no: static
    uint32_t xid = 0x4F534F53;          // "OSOS"
    uint32_t offered = 0, server = 0;

    // DISCOVER
    for (uint32_t i = 0; i < sizeof d; i++) ((uint8_t *)&d)[i] = 0;
    d.op = 1; d.htype = 1; d.hlen = 6;
    d.xid = xid;
    for (int i = 0; i < 6; i++) d.chaddr[i] = our_mac[i];
    d.magic = htonl_(0x63825363);
    int n = 0;
    uint8_t m1 = DHCP_DISCOVER;
    dhcp_append(d.opts, &n, 53, &m1, 1);
    d.opts[n++] = 255;
    // RFC 2131: DHCP messages must be >= 300 bytes; trailing zeros are PAD
    net_udp_send((uint8_t *)&d, sizeof d, IP(255,255,255,255), 67, 68);

    // OFFER
    static struct dhcp_pkt r;
    int rn = net_udp_poll((uint8_t *)&r, sizeof r, 67, 0, 4000);
    if (rn < (int)(44 + 236))
        return -1;
    uint8_t mtype = 0;
    dhcp_get_option(r.opts, (uint32_t)rn - 240, 53, &mtype, 1);
    if (mtype != DHCP_OFFER)
        return -2;
    offered = htonl_(r.yiaddr);
    server = htonl_(r.siaddr);

    // REQUEST
    for (uint32_t i = 0; i < sizeof d; i++) ((uint8_t *)&d)[i] = 0;
    d.op = 1; d.htype = 1; d.hlen = 6;
    d.xid = xid;
    for (int i = 0; i < 6; i++) d.chaddr[i] = our_mac[i];
    d.magic = htonl_(0x63825363);
    n = 0;
    m1 = DHCP_REQUEST;
    dhcp_append(d.opts, &n, 53, &m1, 1);
    uint32_t ip_be = htonl_(offered);
    dhcp_append(d.opts, &n, 50, (uint8_t *)&ip_be, 4);
    uint32_t srv_be = htonl_(server);
    dhcp_append(d.opts, &n, 54, (uint8_t *)&srv_be, 4);
    d.opts[n++] = 255;
    net_udp_send((uint8_t *)&d, sizeof d, IP(255,255,255,255), 67, 68);

    // ACK
    rn = net_udp_poll((uint8_t *)&r, sizeof r, 67, 0, 4000);
    if (rn < (int)(44 + 236))
        return -3;
    mtype = 0;
    dhcp_get_option(r.opts, (uint32_t)rn - 240, 53, &mtype, 1);
    if (mtype != DHCP_ACK)
        return -4;
    our_ip = offered;
    return 0;
}

int net_init(void)
{
    if (e1000_init() != 0)
        return -1;
    e1000_mac(our_mac);
    if (dhcp_run() != 0)
        return -2;
    return 0;
}
