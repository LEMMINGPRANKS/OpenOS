#ifndef OPENOS_PORTIO_H
#define OPENOS_PORTIO_H

#include <stdint.h>

// x86 port I/O for the IO-BAR NIC drivers (rtl8139, rtl8169).

static inline void outb(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" :: "a"(v), "Nd"(port));
}
static inline void outw(uint16_t port, uint16_t v)
{
    __asm__ volatile ("outw %0, %1" :: "a"(v), "Nd"(port));
}
static inline void outl(uint16_t port, uint32_t v)
{
    __asm__ volatile ("outl %0, %1" :: "a"(v), "Nd"(port));
}
static inline uint8_t inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint16_t inw(uint16_t port)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline uint32_t inl(uint16_t port)
{
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

#endif
