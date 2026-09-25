#ifndef OPENOS_IDT_H
#define OPENOS_IDT_H

#include <stdint.h>

void idt_init(void);
void pic_eoi(uint64_t vector);

#endif
