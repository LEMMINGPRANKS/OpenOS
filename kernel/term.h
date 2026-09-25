#ifndef OPENOS_TERM_H
#define OPENOS_TERM_H

#include <stdint.h>

#define TERM_COLOR_WHITE_ON_BLUE 0x1F
#define TERM_COLOR_WHITE_ON_BLACK 0x0F

// Consoles are text grids that render into a pixel viewport. Many can
// exist at once (one per window); term_* calls go to the active one.
struct console;

void term_init(void);
struct console *term_open(uint32_t px, uint32_t py, uint32_t pw, uint32_t ph);
void term_close(struct console *con);
void term_use(struct console *con);        // make active + full redraw
void term_render(struct console *con);     // redraw just this console
void term_move(struct console *con, uint32_t px, uint32_t py); // drag support

void term_putc(char c);
void term_puts(const char *s);
void term_setcolor(uint8_t c);
void term_clear(void);

#endif
