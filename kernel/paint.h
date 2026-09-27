#ifndef OPENOS_PAINT_H
#define OPENOS_PAINT_H

#include <stdint.h>
#include "term.h"

// The Paint app: a pixel canvas in a window. Draws with the mouse, saves
// real .png files onto DR1. The console is only used for the WM's sake;
// all rendering goes straight to the framebuffer.

#define PAINT_W 480
#define PAINT_H 320
#define PAINT_BAR_H 36
#define PAINT_WIN_W (PAINT_W + 2)
#define PAINT_WIN_H (20 + PAINT_H + PAINT_BAR_H + 2)   // title + body

void paint_open(struct console *con);
void paint_input(struct console *con, char c);
void paint_repaint(struct console *con, uint32_t x, uint32_t y,
                   uint32_t w, uint32_t h);
void paint_pointer(struct console *con, int mx, int my, int left);

#endif
