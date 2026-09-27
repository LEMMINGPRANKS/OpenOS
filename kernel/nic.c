#include "nic.h"
#include "e1000.h"
#include "rtl8169.h"
#include "rtl8139.h"
#include "settings.h"

struct nic_driver {
    int  (*init)(void);                 // 0 = found this card and it's up
    void (*mac)(uint8_t *);
    int  (*send)(const uint8_t *, uint32_t);
    int  (*recv)(uint8_t *, uint32_t);
    const char *(*model)(void);
    const char *family;                 // settings name ("nic=e1000")
};

// probe order: e1000 first (QEMU/VirtualBox/VMware default), then the
// Realtek gigabit family most real PCs carry, then the old RTL8139
static const struct nic_driver drivers[] = {
    { e1000_init,   e1000_mac,   e1000_send,   e1000_recv,   e1000_model,   "e1000"   },
    { rtl8169_init, rtl8169_mac, rtl8169_send, rtl8169_recv, rtl8169_model, "rtl8169" },
    { rtl8139_init, rtl8139_mac, rtl8139_send, rtl8139_recv, rtl8139_model, "rtl8139" },
};
#define NDRIVERS (int)(sizeof drivers / sizeof drivers[0])

static const struct nic_driver *active;

int nic_driver_count(void) { return NDRIVERS; }
const char *nic_driver_name(int i)
{
    return (i >= 0 && i < NDRIVERS) ? drivers[i].family : "?";
}

// probe preferred family first, then everything else. A stale preference
// (card pulled, different machine) can never stop us finding a card.
static int probe(const char *pref)
{
    int first = -1;
    if (pref && pref[0]) {
        for (int i = 0; i < NDRIVERS; i++) {
            const char *a = drivers[i].family, *b = pref;
            while (*a && *b && *a == *b) { a++; b++; }
            if (*a == *b && !*a) { first = i; break; }
        }
    }
    if (first >= 0 && drivers[first].init() == 0) {
        active = &drivers[first];
        return 0;
    }
    for (int i = 0; i < NDRIVERS; i++) {
        if (i == first)
            continue;
        if (drivers[i].init() == 0) {
            active = &drivers[i];
            return 0;
        }
    }
    return -1;
}

int nic_init(void)
{
    if (active)
        return 0;
    char pref[16];
    const char *p = 0;
    if (settings_get("nic", pref, sizeof pref) == 0)
        p = pref;
    return probe(p);
}

int nic_rebind(const char *pref)
{
    active = 0;
    return probe(pref && pref[0] ? pref : 0);
}

int nic_up(void) { return active != 0; }

void nic_mac(uint8_t *mac)
{
    if (active)
        active->mac(mac);
}

const char *nic_name(void) { return active ? active->model() : "none"; }

int nic_send(uint8_t *frame, uint32_t len)
{
    if (!active || len > NIC_MTU)
        return -1;
    if (len < NIC_MIN_FRAME) {          // pad runts: real switches drop <60
        for (uint32_t i = len; i < NIC_MIN_FRAME; i++)
            frame[i] = 0;
        len = NIC_MIN_FRAME;
    }
    return active->send(frame, len);
}

int nic_recv(uint8_t *buf, uint32_t max)
{
    return active ? active->recv(buf, max) : -1;
}
