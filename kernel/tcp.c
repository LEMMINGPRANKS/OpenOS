#include "tcp.h"
#include "net.h"
#include "nic.h"
#include "timer.h"

struct tcp_hdr {
    uint16_t sport, dport;
    uint32_t seq, ack;
    uint8_t  doff;                      // data offset (top nibble) + flags byte
    uint8_t  flags;
    uint16_t win, csum, urg;
} __attribute__((packed));

#define FIN 0x01
#define SYN 0x02
#define RST 0x04
#define PSH 0x08
#define ACK 0x10

#define RECV_WINDOW 4096                // what we advertise; also RST sanity range

static uint32_t rem_ip;
static uint16_t rem_port, loc_port;
static uint32_t seq_num, ack_num;
static int connected;
static int peer_fin;

static uint16_t htons_(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static uint16_t ntohs_(uint16_t v) { return htons_(v); }
static uint32_t htonl_(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | (v >> 24);
}
static uint32_t ntohl_(uint32_t v) { return htonl_(v); }

int tcp_connected(void) { return connected; }
uint32_t tcp_remote_ip(void) { return rem_ip; }

static uint16_t tcp_checksum(const struct tcp_hdr *th, const uint8_t *data,
                             uint32_t dlen)
{
    // pseudo header: src ip, dst ip, zero, proto, tcp length
    uint32_t tcp_len = 20 + dlen;
    uint32_t sum = 0;
    // pseudo header: IPs are host-order, so their 16-bit halves ARE the
    // big-endian wire words -- do NOT byte-swap them (that was the SYN bug)
    sum += (net_local_ip() >> 16) & 0xFFFF; sum += net_local_ip() & 0xFFFF;
    sum += (rem_ip >> 16) & 0xFFFF;         sum += rem_ip & 0xFFFF;
    sum += 6;                            // protocol
    sum += tcp_len;

    const uint8_t *p = (const uint8_t *)th;
    for (uint32_t i = 0; i + 1 < 20; i += 2)
        sum += (uint32_t)p[i] << 8 | p[i + 1];
    for (uint32_t i = 0; i + 1 < dlen; i += 2)
        sum += (uint32_t)data[i] << 8 | data[i + 1];
    if (dlen & 1)
        sum += (uint32_t)data[dlen - 1] << 8;
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

static void tcp_send_seg(uint8_t flags, const uint8_t *data, uint32_t len)
{
    struct tcp_hdr th;
    th.sport = htons_(loc_port);
    th.dport = htons_(rem_port);
    th.seq = htonl_(seq_num);
    th.ack = htonl_(ack_num);
    th.doff = (20 / 4) << 4;
    th.flags = flags;
    th.win = htons_(RECV_WINDOW);
    th.csum = 0;
    th.urg = 0;
    th.csum = htons_(tcp_checksum(&th, data, len));
    net_ip_send((uint8_t *)&th, 20, data, len, rem_ip, 6);
}

// wait for a segment matching our ports; fills header + data
struct rxseg {
    struct tcp_hdr hdr;
    uint8_t body[NIC_MTU];
    uint32_t body_len;
};

// signed distance between sequence numbers (they wrap at 2^32)
static int32_t seq_diff(uint32_t a, uint32_t b) { return (int32_t)(a - b); }

static int wait_seg(struct rxseg *out, uint32_t timeout_ms)
{
    static uint8_t buf[NIC_MTU];
    uint64_t start = timer_uptime_ms();
    for (;;) {
        uint32_t src = 0;
        int n = net_ip_poll_tcp(buf, NIC_MTU, &src, 0);
        if (n >= 20 && src == rem_ip) {
            struct tcp_hdr *h = (struct tcp_hdr *)buf;
            uint32_t doff = (uint32_t)(h->doff >> 4) * 4;
            if (ntohs_(h->dport) == loc_port && ntohs_(h->sport) == rem_port &&
                doff >= 20 && doff <= (uint32_t)n) {
                out->hdr = *h;
                uint32_t blen = (uint32_t)n - doff;
                for (uint32_t i = 0; i < blen; i++)
                    out->body[i] = buf[doff + i];
                out->body_len = blen;
                return (int)blen;
            }
        }
        if (timer_uptime_ms() - start > timeout_ms)
            return -1;
        __asm__ volatile ("hlt");
    }
}

int tcp_connect(uint32_t ip, uint16_t port, uint32_t timeout_ms)
{
    static struct rxseg r;               // too big for stack? static to be safe
    rem_ip = ip;
    rem_port = port;
    loc_port = (uint16_t)(10000 + (timer_uptime_ms() & 0xFFF));
    seq_num = timer_uptime_ms() * 9 + 1;
    ack_num = 0;
    connected = 0;
    peer_fin = 0;

    tcp_send_seg(SYN, 0, 0);
    uint64_t start = timer_uptime_ms();
    for (;;) {
        uint64_t used = timer_uptime_ms() - start;
        if (used >= timeout_ms)
            return -1;
        if (wait_seg(&r, timeout_ms - (uint32_t)used) < 0)
            return -1;
        uint8_t f = r.hdr.flags;
        // only a segment acknowledging OUR SYN belongs to this connection
        int acks_syn = (f & ACK) && ntohl_(r.hdr.ack) == seq_num + 1;
        if (f & RST) {
            if (acks_syn)
                return -2;               // refused: nothing listens there
            continue;                    // stray reset from an old connection
        }
        if ((f & (SYN | ACK)) == (SYN | ACK) && acks_syn) {
            ack_num = ntohl_(r.hdr.seq) + 1;
            seq_num += 1;
            tcp_send_seg(ACK, 0, 0);
            connected = 1;
            return 0;
        }
        // anything else (a lone ACK, a stale SYN-ACK): not our handshake
    }
}

int tcp_send(const uint8_t *data, uint32_t len)
{
    if (!connected)
        return -1;
    tcp_send_seg(PSH | ACK, data, len);
    seq_num += len;
    return 0;
}

// Segments are only taken in order: the next byte we expect is ack_num.
// A resent segment (already have it) or one past a gap (lost the one
// before it) is dropped and answered with our current ACK, which makes
// the server resend from exactly where we are.
int tcp_recv(uint8_t *buf, uint32_t max, uint32_t timeout_ms)
{
    static struct rxseg r;
    if (!connected)
        return peer_fin ? -2 : -1;       // closed by peer vs. caller error
    int n = wait_seg(&r, timeout_ms);
    if (n < 0)
        return -1;                       // timeout, still connected
    uint32_t seq = ntohl_(r.hdr.seq);
    int32_t off = seq_diff(ack_num, seq);   // bytes of this segment we have
    if (r.hdr.flags & RST) {
        if (off <= 0 && -off < RECV_WINDOW) { // in-window reset only
            connected = 0;
            return -2;
        }
        return 0;
    }
    if (!r.body_len && !(r.hdr.flags & FIN))
        return 0;                        // bare ACK of what we sent
    uint32_t have = off > 0 ? (uint32_t)off : 0;
    if (off < 0 || (have >= r.body_len && !(r.hdr.flags & FIN))) {
        tcp_send_seg(ACK, 0, 0);         // gap or pure duplicate
        return 0;
    }

    uint32_t len = r.body_len - (have < r.body_len ? have : r.body_len);
    if (len > max)
        len = max;                       // rest isn't ACKed: server resends
    for (uint32_t i = 0; i < len; i++)
        buf[i] = r.body[have + i];
    ack_num += len;

    // the FIN comes after the segment's last byte: only when we took it all
    if ((r.hdr.flags & FIN) && have + len == r.body_len) {
        ack_num += 1;                    // FIN takes one sequence number
        tcp_send_seg(FIN | ACK, 0, 0);
        seq_num += 1;
        connected = 0;
        peer_fin = 1;
        return len ? (int)len : -2;      // data first; next call says closed
    }
    tcp_send_seg(ACK, 0, 0);
    return (int)len;
}

void tcp_close(void)
{
    if (!connected)
        return;
    tcp_send_seg(FIN | ACK, 0, 0);
    seq_num += 1;
    connected = 0;
    static struct rxseg r;
    wait_seg(&r, 300);                   // swallow the final ACK
}
