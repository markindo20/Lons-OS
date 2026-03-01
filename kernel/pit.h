// kernel/pit.h
#ifndef KERNEL_PIT_H
#define KERNEL_PIT_H

#include <stdint.h>

#define PIT_HZ 100  // 100 ticks per second

void     pit_init(void);
void     pit_irq_handler(void);
uint64_t pit_get_ticks(void);
uint64_t pit_get_seconds(void);
void     pit_sleep_ms(uint64_t ms);

#endif