#include "appbar.h"
#include "gfx.h"
#include "font.h"

#define BAR_BG     0xF7F7FA
#define BAR_LINE   0xD8D8DE
#define BAR_TITLE  0x28282E
#define BAR_STATUS 0x8A8A92
#define BAR_H      (APPBAR_ROWS * FONT_H)   // 48px

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

void appbar_paint(struct console *con, const char *title,
                  const char *status, uint32_t accent)
{
    uint32_t vx, vy, vw, vh;
    term_view(con, &vx, &vy, &vw, &vh);
    (void)vh;

    gfx_fill_rect(vx, vy, vw, BAR_H, BAR_BG);
    gfx_fill_rect(vx, vy + BAR_H, vw, 1, BAR_LINE);

    // accent dot + title
    gfx_fill_circle(vx + 22, vy + BAR_H / 2, 6, accent);
    gfx_text(vx + 38, vy + (BAR_H - FONT_H) / 2, title,
             BAR_TITLE, BAR_BG);

    // status, right-aligned
    if (status && status[0]) {
        int n = str_len(status);
        uint32_t sw = (uint32_t)n * FONT_W;
        gfx_text(vx + vw - sw - 16, vy + (BAR_H - FONT_H) / 2, status,
                 BAR_STATUS, BAR_BG);
    }
}
