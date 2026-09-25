#ifndef OPENOS_WM_H
#define OPENOS_WM_H

#include <stdint.h>
#include "apps.h"

#define WM_MAX_WINDOWS 8

struct window {
    uint8_t used;
    enum app_id app;
    struct console *con;
    uint32_t x, y, w, h;
};

void wm_init(void (*repaint_cb)(void));     // desktop's bg+taskbar repaint
int  wm_open(enum app_id app, uint32_t x, uint32_t y,
             uint32_t w, uint32_t h);       // 0 = ok, -1 = full
void wm_close_focused(void);
int  wm_any_open(void);
int  wm_focused_app(void);                  // -1 if none
void wm_paint_all(void);                    // bg already painted by desktop
void wm_key(char c);                        // ESC closes focused, else route
void wm_mouse(int mx, int my, uint8_t buttons);

#endif
