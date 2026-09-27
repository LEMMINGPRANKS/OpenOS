#include "banner.h"
#include "gfx.h"
#include "font.h"
#include "timer.h"

// App start banners, after the Wii: a rounded card in the app's colour
// with its name and three bouncing dots while it "loads". The static card
// is drawn once; only the dot strip and shimmer bar are redrawn per frame,
// so this stays cheap even at 30 fps.

#define BAN_W        480
#define BAN_H        270
#define BAN_RAD      14
#define BAN_MS       1100
#define DOT_R        7
#define DOT_STRIP_Y  150
#define SHIM_Y       200
#define SHIM_W       240
#define SHIM_H       8

void banner_play(enum app_id app)
{
    uint32_t sw = gfx_width(), sh = gfx_height();
    uint32_t x = (sw - BAN_W) / 2;
    uint32_t y = (sh - BAN_H) / 2 - 30;
    uint32_t accent = app_accent(app);

    // static card
    gfx_shadow_r(x, y, BAN_W, BAN_H, BAN_RAD);
    gfx_fill_rect_r(x, y, BAN_W, BAN_H, BAN_RAD, 0xFFFFFF);
    gfx_fill_rect_top_r(x, y, BAN_W, 70, BAN_RAD, accent);
    const char *nm = app_name(app);
    int tw = 0;
    while (nm[tw]) tw++;
    gfx_text(x + (BAN_W - tw * FONT_W) / 2, y + 26, nm, 0xFFFFFF, accent);
    gfx_text_fg(x + (BAN_W - 9 * FONT_W) / 2, y + 92, "starting", 0x8A8A92);
    gfx_fill_rect_r(x + (BAN_W - SHIM_W) / 2, y + SHIM_Y, SHIM_W, SHIM_H, 4,
                    0xE6E6EC);

    uint64_t start = timer_uptime_ms();
    for (;;) {
        uint64_t t = timer_uptime_ms() - start;
        if (t >= BAN_MS)
            break;

        // three bouncing dots (triangle-wave, staggered by 1/3 phase)
        gfx_fill_rect(x + BAN_W / 2 - 60, y + DOT_STRIP_Y - 24, 120, 48,
                      0xFFFFFF);
        for (int i = 0; i < 3; i++) {
            uint32_t ph = (uint32_t)((t / 4 + i * 85) % 256);
            int up = ph < 128;
            uint32_t f = up ? ph : 255 - ph;        // 0..127
            int dy = (int)(f * 18 / 127);           // 0..18
            gfx_fill_circle(x + BAN_W / 2 - 24 + i * 24,
                            y + DOT_STRIP_Y + dy, DOT_R, accent);
        }

        // shimmer: a bright segment sweeping across the bar
        gfx_fill_rect_r(x + (BAN_W - SHIM_W) / 2, y + SHIM_Y, SHIM_W, SHIM_H,
                        4, 0xE6E6EC);
        uint32_t sweep = (uint32_t)((t * SHIM_W / 500) % (SHIM_W + 60));
        int sx = (int)sweep - 30;
        uint32_t bx = x + (BAN_W - SHIM_W) / 2;
        if (sx < 0) sx = 0;
        uint32_t seg = 60;
        if ((uint32_t)sx + seg > SHIM_W)
            seg = SHIM_W - (uint32_t)sx;
        if (seg)
            gfx_fill_rect(bx + (uint32_t)sx, y + SHIM_Y, seg, SHIM_H, accent);

        uint64_t frame = timer_uptime_ms();
        while (timer_uptime_ms() - frame < 33)
            __asm__ volatile ("hlt");
    }
}
