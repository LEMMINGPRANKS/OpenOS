#include "nic.h"
#include "e1000.h"
#include "rtl8169.h"
#include "rtl8139.h"

struct nic_driver {
    int  (*init)(void);                 // 0 = found this card and it's up
    void (*mac)(uint8_t *);
    int  (*send)(const uint8_t *, uint32_t);
    int  (*recv)(uint8_t *, uint32_t);
    const char *(*model)(void);
};

// probe order: e1000 first (QEMU/VirtualBox/VMware default), then the
// Realtek gigabit family most real PCs carry, then the old RTL8139
static const struct nic_driver drivers[] = {
    { e1000_init,   e1000_mac,   e1000_send,   e1000_recv,   e1000_model   },
    { rtl8169_init, rtl8169_mac, rtl8169_send, rtl8169_recv, rtl8169_model },
    { rtl8139_init, rtl8139_mac, rtl8139_send, rtl8139_recv, rtl8139_model },
};
#define NDRIVERS (int)(sizeof drivers / sizeof drivers[0])

static const struct nic_driver *active;

int nic_init(void)
{
    if (active)
        return 0;
    for (int i = 0; i < NDRIVERS; i++)
        if (drivers[i].init() == 0) {
            active = &drivers[i];
            return 0;
        }
    return -1;
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
