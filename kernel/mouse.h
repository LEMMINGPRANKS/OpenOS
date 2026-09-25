#ifndef OPENOS_MOUSE_H
#define OPENOS_MOUSE_H

#include <stdint.h>

void mouse_init(void);
void mouse_on_irq(void);
void mouse_get(int32_t *x, int32_t *y, uint8_t *buttons);

#endif
