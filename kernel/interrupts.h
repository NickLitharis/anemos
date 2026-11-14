#ifndef ANEMOS_KERNEL_INTERRUPTS_H
#define ANEMOS_KERNEL_INTERRUPTS_H

#include <stdint.h>

int interrupts_init(void);
void timer_init(void);
uint64_t timer_ticks(void);
void interrupt_dispatch(void *raw_frame);

#endif
