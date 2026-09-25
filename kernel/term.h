#ifndef OPENOS_TERM_H
#define OPENOS_TERM_H

#include <stdint.h>

#define TERM_COLS 80
#define TERM_ROWS 25
#define TERM_COLOR_WHITE_ON_BLUE 0x1F
#define TERM_COLOR_WHITE_ON_BLACK 0x0F

void term_init(void);
void term_setcolor(uint8_t color);
void term_putc(char c);
void term_puts(const char *s);
void term_clear(void);

#endif
