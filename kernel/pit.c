// kernel/pit.c
// Programmable Interval Timer (Intel 8253/8254) — Channel 0, Mode 3 (Square Wave)

#include "pit.h"
#include "io.h"

#define PIT_CHANNEL0_DATA  0x40
#define PIT_COMMAND        0x43

// PIT base frequency is 1,193,182 Hz
#define PIT_BASE_FREQ      1193182

// Global tick counter — incremented by IRQ0 handler
static volatile uint64_t pit_ticks = 0;

void pit_init(void) {
    // Calculate divisor for desired frequency
    // divisor = base_freq / desired_freq
    uint16_t divisor = (uint16_t)(PIT_BASE_FREQ / PIT_HZ);

    // Command byte: channel 0, lobyte/hibyte access, mode 3 (square wave), binary
    // Bits: 00 11 011 0 = 0x36
    outb(PIT_COMMAND, 0x36);

    // Send divisor (low byte first, then high byte)
    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));

    pit_ticks = 0;
}

// Called from irq_handler() in idt.c on every IRQ0
void pit_irq_handler(void) {
    pit_ticks++;
}

// Get raw tick count since boot
uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

// Get seconds since boot
uint64_t pit_get_seconds(void) {
    return pit_ticks / PIT_HZ;
}

// Busy-wait sleep (blocking) — good enough until you have a scheduler
void pit_sleep_ms(uint64_t ms) {
    uint64_t target = pit_ticks + (ms * PIT_HZ) / 1000;
    while (pit_ticks < target) {
        __asm__ volatile ("hlt");  // sleep until next interrupt
    }
}