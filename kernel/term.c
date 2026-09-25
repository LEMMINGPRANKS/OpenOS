#include "term.h"

#define VGA_MEM ((volatile uint16_t *)0xB8000)
#define COM1_PORT 0x3F8

static int row;
static int col;
static uint8_t color = TERM_COLOR_WHITE_ON_BLACK;

static void serial_init(void)
{
    __asm__ volatile ("outb %%al, %%dx" :: "a"(0x03), "d"(COM1_PORT + 1)); // no interrupts
    __asm__ volatile ("outb %%al, %%dx" :: "a"(0x80), "d"(COM1_PORT + 3)); // DLAB on
    __asm__ volatile ("outb %%al, %%dx" :: "a"(0x01), "d"(COM1_PORT + 0)); // 115200 baud
    __asm__ volatile ("outb %%al, %%dx" :: "a"(0x00), "d"(COM1_PORT + 1));
    __asm__ volatile ("outb %%al, %%dx" :: "a"(0x03), "d"(COM1_PORT + 3)); // 8N1
    __asm__ volatile ("outb %%al, %%dx" :: "a"(0xC7), "d"(COM1_PORT + 2)); // FIFO
    __asm__ volatile ("outb %%al, %%dx" :: "a"(0x0B), "d"(COM1_PORT + 4)); // RTS/DSR
}

static void serial_putc(char c)
{
    int ready = 0;
    while (!ready) {
        uint8_t st;
        __asm__ volatile ("inb %%dx, %%al" : "=a"(st) : "d"(COM1_PORT + 5));
        ready = st & 0x20;
    }
    __asm__ volatile ("outb %%al, %%dx" :: "a"((uint8_t)c), "d"(COM1_PORT));
}

static void scroll(void)
{
    for (int i = 0; i < TERM_COLS * (TERM_ROWS - 1); i++)
        VGA_MEM[i] = VGA_MEM[i + TERM_COLS];
    for (int i = TERM_COLS * (TERM_ROWS - 1); i < TERM_COLS * TERM_ROWS; i++)
        VGA_MEM[i] = (uint16_t)(color << 8 | ' ');
    row = TERM_ROWS - 1;
}

void term_init(void)
{
    serial_init();
    row = 0;
    col = 0;
    term_clear();
}

void term_setcolor(uint8_t c)
{
    color = c;
}

void term_putc(char c)
{
    serial_putc(c);
    if (c == '\n') {
        col = 0;
        if (++row >= TERM_ROWS) scroll();
        return;
    }
    VGA_MEM[row * TERM_COLS + col] = (uint16_t)(color << 8 | (uint8_t)c);
    if (++col >= TERM_COLS) {
        col = 0;
        if (++row >= TERM_ROWS) scroll();
    }
}

void term_puts(const char *s)
{
    while (*s)
        term_putc(*s++);
}

void term_clear(void)
{
    for (int i = 0; i < TERM_COLS * TERM_ROWS; i++)
        VGA_MEM[i] = (uint16_t)(color << 8 | ' ');
    row = 0;
    col = 0;
}
