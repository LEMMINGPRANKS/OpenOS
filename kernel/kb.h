#ifndef OPENOS_KB_H
#define OPENOS_KB_H

#include <stdint.h>

void kb_on_scancode(uint8_t sc);
char kb_getchar(void);
int kb_haschar(void);

// arrow keys arrive as these codes (E0-prefixed scancodes)
#define KEY_UP    1
#define KEY_DOWN  2
#define KEY_LEFT  3
#define KEY_RIGHT 4
// shift variants for the terminal scrollback (E0 + shift)
#define KEY_SUP    5   // shift+up:   one line back
#define KEY_SDOWN  6   // shift+down: one line forward
#define KEY_SPGUP  7   // shift+pgup: half a page back
#define KEY_SPGDN  8   // shift+pgdn: half a page forward
#define KEY_SHOME  9   // shift+home: jump to the top
#define KEY_SEND  10   // shift+end:  back to live

#endif
