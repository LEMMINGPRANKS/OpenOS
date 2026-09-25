#include "dev.h"
#include "term.h"
#include "panic.h"
#include "initrd.h"

struct device {
    const char *name;
    const char *desc;
    uint8_t present;
    uint8_t trap;                  // UR-style: watchdog device
};

static struct device devs[] = {
    { "DR1", "main drive",              0, 0 },
    { "DR2", "sd card slot",            0, 0 },
    { "DR3", "usb slot",                0, 0 },
    { "IR1", "ram",                     1, 0 },
    { "IR2", "initramfs",               0, 0 },   // set live below
    { "UR1", "unsupported-device trap", 1, 1 },
};

#define NDEVS (sizeof(devs) / sizeof(devs[0]))

static volatile int ur1_fired;

static int same_ci(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca -= 32;
        if (cb >= 'a' && cb <= 'z') cb -= 32;
        if (ca != cb)
            return 0;
        a++; b++;
    }
    return *a == *b;
}

void dev_list(void)
{
    devs[4].present = (uint8_t)initrd_ok();   // IR2
    term_puts("device registers:\n");
    for (unsigned i = 0; i < NDEVS; i++) {
        term_puts("  ");
        term_puts(devs[i].name);
        term_puts("  ");
        term_puts(devs[i].desc);
        term_puts(devs[i].trap ? "  [trap, " : "  [");
        term_puts(devs[i].present ? "present]" : "empty]");
        term_putc('\n');
    }
}

// Called every timer tick: the UR1 watchdog checking for bad devices.
void ur1_poll(void)
{
    if (ur1_fired)
        kpanic("UR1 FIRED: unsupported device detected.");
}

int dev_fire(const char *name)
{
    if (same_ci(name, "UR1")) {
        if (ur1_fired)
            return 1;
        ur1_fired = 1;             // watchdog will catch it within one tick
        return 0;
    }
    return -1;
}
