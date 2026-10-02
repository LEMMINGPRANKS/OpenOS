#ifndef OPENOS_TERM_H
#define OPENOS_TERM_H

#include <stdint.h>

#define TERM_COLOR_WHITE_ON_BLUE 0x1F
#define TERM_COLOR_DIR           0x1E   // yellow on blue, for directories

// Consoles are text grids that render into a pixel viewport. Many can
// exist at once (one per window); term_* calls go to the active one.
struct console;

void term_init(void);
struct console *term_open(uint32_t px, uint32_t py, uint32_t pw, uint32_t ph);
void term_close(struct console *con);
void term_use(struct console *con);        // make active + full redraw
struct console *term_active(void);
void term_render(struct console *con);     // redraw just this console
void term_move(struct console *con, uint32_t px, uint32_t py); // drag support
void term_view(const struct console *con, uint32_t *x, uint32_t *y,
               uint32_t *w, uint32_t *h);                       // pixel viewport
int term_goto(struct console *con, int col, int row);  // move the write cursor
uint16_t term_cols(struct console *con);
uint16_t term_visible_rows(struct console *con); // text rows below the appbar
void term_pos(struct console *con, int *col, int *row);
int term_locate(struct console *con, int mx, int my, int *col, int *row);
void term_protect(struct console *con, int rows);  // pixel-toolbar rows

// scrollback: +lines = back in time, -lines = forward to live
void term_scroll_by(struct console *con, int lines);
void term_scroll_home(struct console *con); // jump to the oldest line
void term_scroll_end(struct console *con);  // back to the live prompt
uint16_t term_scroll_view(const struct console *con); // lines back (0 = live)

void term_putc(char c);
void term_puts(const char *s);
void term_setcolor(uint8_t c);
void term_clear(void);

#endif
