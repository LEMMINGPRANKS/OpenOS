#include "dns.h"
#include "net.h"
#include "timer.h"

// A from-scratch DNS client: build one A-record question, send it over
// UDP, parse the first A answer out of the reply.

#define DNS_PORT        53
#define DNS_SRC_PORT    5353           // our source port (mDNS-adjacent)
#define DNS_TRIES       2
#define DNS_TIMEOUT_MS  3000
#define DNS_MAX_NAME    64
#define DNS_CACHE       4

static uint32_t server_ip = IP(10,0,2,3);   // QEMU user-net resolver

static struct { char name[DNS_MAX_NAME]; uint32_t ip; } cache[DNS_CACHE];

void dns_set_server(uint32_t ip) { server_ip = ip; }
uint32_t dns_server(void)        { return server_ip; }

static uint16_t flips(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }

static int name_len(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

// copy into cache, evicting the first slot when full
static void cache_add(const char *name, uint32_t ip)
{
    int slot = DNS_CACHE - 1;
    for (int i = 0; i < DNS_CACHE; i++)
        if (!cache[i].name[0]) { slot = i; break; }
    for (int i = 0; i < DNS_MAX_NAME - 1 && name[i]; i++)
        cache[slot].name[i] = name[i];
    cache[slot].name[DNS_MAX_NAME - 1] = 0;
    cache[slot].ip = ip;
}

// encode "www.example.com" as DNS QNAME labels; returns bytes written
static int build_qname(uint8_t *out, const char *name)
{
    int n = name_len(name);
    if (n >= DNS_MAX_NAME)
        return 0;
    int pos = 0;
    int label = 0;
    for (int i = 0; i <= n; i++) {
        if (name[i] == '.' || name[i] == 0) {
            if (label == 0 || label > 63)
                return 0;               // empty or over-long label
            out[pos++] = (uint8_t)label;
            for (int k = i - label; k < i; k++)
                out[pos++] = (uint8_t)name[k];
            label = 0;
        } else {
            label++;
        }
    }
    out[pos++] = 0;                     // root label
    return pos;
}

// skip a possibly-compressed domain name; returns bytes consumed, -1 on junk
static int skip_name(const uint8_t *p, int len, int pos)
{
    for (;;) {
        if (pos >= len)
            return -1;
        uint8_t b = p[pos];
        if (b == 0)
            return pos + 1;
        if ((b & 0xC0) == 0xC0)
            return pos + 2;             // compression pointer: 2 bytes total
        pos += 1 + b;
    }
}

static uint32_t parse_reply(const uint8_t *p, int len, uint16_t want_id)
{
    if (len < 12)
        return 0;
    if (flips(*(uint16_t *)(void *)&p[0]) != want_id)
        return 0;
    uint16_t flags = flips(*(uint16_t *)(void *)&p[2]);
    if (!(flags & 0x8000))              // not a response
        return 0;
    if (flags & 0x000F)                 // RCODE != 0
        return 0;
    uint16_t answers = flips(*(uint16_t *)(void *)&p[6]);
    if (!answers)
        return 0;

    int pos = skip_name(p, len, 12);    // the question...
    if (pos < 0 || pos + 4 > len)
        return 0;
    pos += 4;                           // ...and its type+class

    for (uint16_t a = 0; a < answers; a++) {
        pos = skip_name(p, len, pos);
        if (pos < 0 || pos + 10 > len)
            return 0;
        uint16_t type = flips(*(uint16_t *)(void *)&p[pos]);
        uint16_t rdlen = flips(*(uint16_t *)(void *)&p[pos + 8]);
        pos += 10;
        if (pos + rdlen > len)
            return 0;
        if (type == 1 && rdlen == 4)    // A record: 4 bytes of IPv4
            return ((uint32_t)p[pos] << 24) | ((uint32_t)p[pos + 1] << 16) |
                   ((uint32_t)p[pos + 2] << 8) | (uint32_t)p[pos + 3];
        pos += rdlen;
    }
    return 0;
}

uint32_t dns_resolve(const char *name)
{
    if (!name || !name[0])
        return 0;
    for (int i = 0; i < DNS_CACHE; i++)
        if (cache[i].name[0]) {
            int j = 0;
            while (name[j] && cache[i].name[j] == name[j]) j++;
            if (!name[j] && !cache[i].name[j])
                return cache[i].ip;
        }

    static uint8_t q[512];
    static uint8_t r[512];

    int qn = build_qname(q + 12, name);
    if (qn == 0)
        return 0;

    static uint16_t id_counter;
    uint16_t id = (uint16_t)(timer_uptime_ms() ^ (id_counter += 0x1357));

    // header: id, flags = recursion desired, 1 question, no others
    *(uint16_t *)(void *)&q[0]  = flips(id);
    *(uint16_t *)(void *)&q[2]  = flips(0x0100);
    *(uint16_t *)(void *)&q[4]  = flips(1);
    *(uint16_t *)(void *)&q[6]  = 0;
    *(uint16_t *)(void *)&q[8]  = 0;
    *(uint16_t *)(void *)&q[10] = 0;
    int qlen = 12 + qn;
    *(uint16_t *)(void *)&q[qlen]      = flips(1);   // QTYPE = A
    *(uint16_t *)(void *)&q[qlen + 2]  = flips(1);   // QCLASS = IN
    qlen += 4;

    for (int t = 0; t < DNS_TRIES; t++) {
        if (net_udp_send(q, (uint32_t)qlen, server_ip, DNS_PORT,
                         DNS_SRC_PORT) != 0)
            continue;
        int rn = net_udp_poll(r, sizeof r, DNS_PORT, server_ip,
                              DNS_TIMEOUT_MS);
        if (rn > 0) {
            uint32_t ip = parse_reply(r, rn, id);
            if (ip) {
                cache_add(name, ip);
                return ip;
            }
        }
    }
    return 0;
}
