#include "music.h"
#include "term.h"
#include "timer.h"
#include "portio.h"
#include "appbar.h"
#include "gfx.h"
#include "font.h"

// The Music app. The PC speaker is driven by PIT channel 2: load a divisor
// (1193180 / freq) and open the gate on port 0x61. The window is a stack of
// pixel cards: tune buttons you can click, a piano-key card for free play,
// and a text area underneath for what's playing.

#define PIT_CMD   0x43
#define PIT_CH2   0x42
#define SPEAKER   0x61
#define PIT_BASE  1193180UL

#define MUSIC_ACCENT  0xD81B60

#define CARD_X_PAD    16
#define CARD_H        44
#define CARD_GAP      10
#define KEYS_CARD_H   64
#define KEY_BOX       26
#define STATUS_ROW    14            // first text row under the cards

// semitone index 0 = C4 ... 35 = B6 (3 octaves, equal temperament)
static const uint16_t note_freq[36] = {
    262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
    523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988,
    1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976
};

#define REST (-1)

struct note {
    int8_t semi;       // semitone index (C4 = 0) or REST
    uint8_t beats;     // duration in beats
};

static const struct note tune_ode[] = {
    {4,1},{4,1},{5,1},{7,1},{7,1},{5,1},{4,1},{2,1},
    {0,1},{0,1},{2,1},{4,1},{4,2},{2,2},
    {4,1},{4,1},{5,1},{7,1},{7,1},{5,1},{4,1},{2,1},
    {0,1},{0,1},{2,1},{4,1},{2,2},{0,2},
};
static const struct note tune_twinkle[] = {
    {0,1},{0,1},{7,1},{7,1},{9,1},{9,1},{7,2},
    {5,1},{5,1},{4,1},{4,1},{2,1},{2,1},{0,2},
};
static const struct note tune_fanfare[] = {
    {0,1},{4,1},{7,1},{12,2},{REST,1},
    {7,1},{12,1},{16,3},{REST,1},
    {14,1},{12,1},{14,1},{16,2},{12,2},{0,2},
};

static const struct tune {
    const char *name;
    const struct note *notes;
    int count;
    uint16_t beat_ms;
} tunes[] = {
    { "Ode to Joy",   tune_ode,      sizeof tune_ode / sizeof *tune_ode,      280 },
    { "Twinkle",      tune_twinkle,  sizeof tune_twinkle / sizeof *tune_twinkle, 300 },
    { "OpenOS theme", tune_fanfare,  sizeof tune_fanfare / sizeof *tune_fanfare, 220 },
};
#define TUNE_COUNT (sizeof tunes / sizeof *tunes)
#define FREE_KEYS  "awsedftgyhujk"
#define FREE_COUNT 13

static void spk_freq(uint32_t freq)
{
    outb(PIT_CMD, 0xB6);
    uint16_t div = (uint16_t)(PIT_BASE / freq);
    outb(PIT_CH2, div & 0xFF);
    outb(PIT_CH2, (uint8_t)(div >> 8));
    outb(SPEAKER, inb(SPEAKER) | 3);    // gate on
}

static void spk_off(void)
{
    outb(SPEAKER, inb(SPEAKER) & (uint8_t)~3);
}

static void wait_ms(uint32_t ms)
{
    uint64_t start = timer_uptime_ms();
    while (timer_uptime_ms() - start < ms)
        __asm__ volatile ("hlt");
}

static int str_len(const char *s) { int n = 0; while (s[n]) n++; return n; }

// --- pixel cards -----------------------------------------------------------

static void card_rect(struct console *con, int i, uint32_t *x, uint32_t *y,
                      uint32_t *w)
{
    uint32_t vx, vy, vw, vh;
    term_view(con, &vx, &vy, &vw, &vh);
    (void)vh;
    *x = vx + CARD_X_PAD;
    *w = vw - 2 * CARD_X_PAD;
    *y = vy + APPBAR_ROWS * FONT_H + 8 + (uint32_t)i * (CARD_H + CARD_GAP);
}

static void keys_rect(struct console *con, uint32_t *x, uint32_t *y,
                      uint32_t *w)
{
    uint32_t vx, vy, vw, vh;
    term_view(con, &vx, &vy, &vw, &vh);
    (void)vh;
    *x = vx + CARD_X_PAD;
    *w = vw - 2 * CARD_X_PAD;
    *y = vy + APPBAR_ROWS * FONT_H + 8 +
         (uint32_t)TUNE_COUNT * (CARD_H + CARD_GAP) + 6;
}

static void cards_paint(struct console *con)
{
    for (uint32_t i = 0; i < TUNE_COUNT; i++) {
        uint32_t x, y, w;
        card_rect(con, (int)i, &x, &y, &w);
        gfx_fill_rect_r(x, y, w, CARD_H, 10, 0xFFFFFF);
        gfx_rect_r(x, y, w, CARD_H, 10, 0xDEDEE4);
        // number badge
        gfx_fill_circle(x + 26, y + CARD_H / 2, 13, MUSIC_ACCENT);
        char num[2] = { (char)('1' + i), 0 };
        gfx_text(x + 26 - FONT_W / 2, y + (CARD_H - FONT_H) / 2, num,
                 0xFFFFFF, MUSIC_ACCENT);
        gfx_text(x + 48, y + (CARD_H - FONT_H) / 2, tunes[i].name,
                 0x28282E, 0xFFFFFF);
        const char *hint = "click or press the number";
        gfx_text(x + w - (uint32_t)str_len(hint) * FONT_W - 14,
                 y + (CARD_H - FONT_H) / 2, hint, 0x9AA0A6, 0xFFFFFF);
    }
    // the free-play keyboard card
    uint32_t kx, ky, kw;
    keys_rect(con, &kx, &ky, &kw);
    gfx_fill_rect_r(kx, ky, kw, KEYS_CARD_H, 10, 0xF7F7FA);
    gfx_rect_r(kx, ky, kw, KEYS_CARD_H, 10, 0xE3E3E9);
    const char *cap = "free play -- click or type";
    gfx_text(kx + 14, ky + 6, cap, 0x8A8A92, 0xF7F7FA);
    uint32_t row_w = FREE_COUNT * KEY_BOX + (FREE_COUNT - 1) * 4;
    uint32_t sx = kx + (kw - row_w) / 2;
    uint32_t sy = ky + 26;
    for (int i = 0; i < FREE_COUNT; i++) {
        uint32_t bx = sx + (uint32_t)i * (KEY_BOX + 4);
        gfx_fill_rect_r(bx, sy, KEY_BOX, KEY_BOX, 6, 0xFFFFFF);
        gfx_rect_r(bx, sy, KEY_BOX, KEY_BOX, 6, 0xDEDEE4);
        char ch[2] = { FREE_KEYS[i], 0 };
        gfx_text(bx + (KEY_BOX - FONT_W) / 2, sy + (KEY_BOX - FONT_H) / 2,
                 ch, 0x28282E, 0xFFFFFF);
    }
}

// --- playing ---------------------------------------------------------------

static void play_tune(struct console *con, int t)
{
    const struct tune *tu = &tunes[t];
    term_goto(con, 0, STATUS_ROW);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("playing ");
    term_setcolor(0x05);
    term_puts(tu->name);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("...\n");
    for (int i = 0; i < tu->count; i++) {
        const struct note *n = &tu->notes[i];
        uint32_t ms = (uint32_t)tu->beat_ms * n->beats;
        if (n->semi == REST) {
            spk_off();
        } else {
            spk_freq(note_freq[n->semi]);
            wait_ms(ms - 20);           // tiny gap so repeated notes speak
            spk_off();
            wait_ms(20);
        }
    }
    term_puts("(done -- space for silence)\n");
}

void music_app_open(struct console *con)
{
    term_use(con);
    term_protect(con, STATUS_ROW);
    term_clear();
    appbar_paint(con, "Music", "PC speaker", MUSIC_ACCENT);
    cards_paint(con);
    term_goto(con, 0, STATUS_ROW);
    term_setcolor(TERM_COLOR_WHITE_ON_BLUE);
    term_puts("pick a tune above, or jam on the keys.\n");
}

void music_app_repaint(struct console *con)
{
    cards_paint(con);                   // cards are protected rows, but be safe
}

void music_app_input(struct console *con, char c)
{
    static const char keys[] = FREE_KEYS;
    term_use(con);
    if (c == ' ') {
        spk_off();
        return;
    }
    for (int i = 0; keys[i]; i++) {
        if (c == keys[i]) {
            spk_freq(note_freq[i]);
            return;
        }
    }
    if (c >= '1' && c <= (char)('0' + TUNE_COUNT)) {
        play_tune(con, c - '1');
        return;
    }
}

// click a tune card to play it, or a key box to beep that note
void music_app_click(struct console *con, int mx, int my)
{
    for (uint32_t i = 0; i < TUNE_COUNT; i++) {
        uint32_t x, y, w;
        card_rect(con, (int)i, &x, &y, &w);
        if (mx >= (int)x && mx < (int)(x + w) &&
            my >= (int)y && my < (int)(y + CARD_H)) {
            play_tune(con, (int)i);
            return;
        }
    }
    uint32_t kx, ky, kw;
    keys_rect(con, &kx, &ky, &kw);
    if (my >= (int)(ky + 26) && my < (int)(ky + 26 + KEY_BOX) &&
        mx >= (int)kx && mx < (int)(kx + kw)) {
        uint32_t row_w = FREE_COUNT * KEY_BOX + (FREE_COUNT - 1) * 4;
        uint32_t sx = kx + (kw - row_w) / 2;
        int rel = mx - (int)sx;
        int idx = rel / (KEY_BOX + 4);
        if (idx >= 0 && idx < FREE_COUNT && rel - idx * (KEY_BOX + 4) < KEY_BOX)
            spk_freq(note_freq[idx]);
    }
}
