// kernel/pit.c
#include "pit.h"
#include "io.h"
#include "pic.h"   // Added to fix pic_send_eoi error
#include "sched.h" // Added to fix sched_yield error

#define PIT_CHANNEL0_DATA  0x40
#define PIT_COMMAND        0x43
#define PIT_BASE_FREQ      1193182

static volatile uint64_t pit_ticks = 0;

void pit_init(void) {
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / PIT_HZ);
    outb(PIT_COMMAND, 0x36);
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    pit_ticks = 0;
}

// We merged pit_irq_handler and pit_handler into one clean function
void pit_handler(void) {
    pit_ticks++;

    // Every 10 ticks (approx 100ms if PIT_HZ is 100), swap tasks
    if (pit_ticks % 10 == 0) {
        sched_yield();
    }

    // Must match the name in pic.h exactly
    pic_send_eoi(0); 
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

uint64_t pit_get_seconds(void) {
    return pit_ticks / PIT_HZ;
}

void pit_sleep_ms(uint64_t ms) {
    uint64_t target = pit_ticks + (ms * PIT_HZ) / 1000;
    while (pit_ticks < target) {
        __asm__ volatile ("hlt");
    }
}

// Add this so your existing IDT code can still find it!
void pit_irq_handler(void) {
    pit_handler();
}