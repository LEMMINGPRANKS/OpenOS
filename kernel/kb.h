#ifndef OPENOS_KB_H
#define OPENOS_KB_H

#include <stdint.h>

void kb_on_scancode(uint8_t sc);
char kb_getchar(void);
int kb_haschar(void);

#endif
