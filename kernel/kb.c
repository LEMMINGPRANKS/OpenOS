#include "kb.h"

// PS/2 keyboard, scancode set 1, US QWERTY.

#define BUF_SIZE 256

static const char map_lo[128] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0,
};

static const char map_hi[128] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0,
};

static char buf[BUF_SIZE];
static volatile uint32_t head;
static volatile uint32_t tail;
static int shift;
static int extended;

static void push(char c)
{
    uint32_t next = (head + 1) % BUF_SIZE;
    if (next == tail)
        return;                        // full: drop the key
    buf[head] = c;
    head = next;
}

void kb_on_scancode(uint8_t sc)
{
    if (sc == 0xE0) {                  // arrow keys etc: two-byte codes
        extended = 1;
        return;
    }
    if (extended) {
        extended = 0;
        if (sc & 0x80)
            return;                    // key release
        if (sc == 0x48) push(shift ? KEY_SUP : KEY_UP);
        else if (sc == 0x50) push(shift ? KEY_SDOWN : KEY_DOWN);
        else if (sc == 0x4B) push(KEY_LEFT);
        else if (sc == 0x4D) push(KEY_RIGHT);
        else if (sc == 0x49 && shift) push(KEY_SPGUP);   // pgup
        else if (sc == 0x51 && shift) push(KEY_SPGDN);   // pgdn
        else if (sc == 0x47 && shift) push(KEY_SHOME);   // home
        else if (sc == 0x4F && shift) push(KEY_SEND);    // end
        return;
    }
    if (sc == 0x2A || sc == 0x36) { shift = 1; return; }
    if (sc == 0xAA || sc == 0xB6) { shift = 0; return; }
    if (sc & 0x80)
        return;                        // key release of another key
    char c = shift ? map_hi[sc] : map_lo[sc];
    if (c)
        push(c);
}

int kb_haschar(void)
{
    return head != tail;
}

char kb_getchar(void)
{
    while (!kb_haschar())
        __asm__ volatile ("hlt");      // sleep until an interrupt wakes us
    char c = buf[tail];
    tail = (tail + 1) % BUF_SIZE;
    return c;
}
