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

#endif
