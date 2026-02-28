#pragma once
#include <stdint.h>

/*
 * keyboard.h — PS/2 Keyboard Driver
 *
 * Reads scan codes from port 0x60.
 * Converts US QWERTY scan codes to ASCII.
 * Stores keypresses in a ring buffer.
 *
 * Usage:
 *   keyboard_init();            // call once during boot
 *   while (!keyboard_haskey()); // wait for input
 *   char c = keyboard_getchar(); // read one character
 */

/* Ring buffer capacity — power of 2 for fast modulo */
#define KEYBOARD_BUFFER_SIZE 256

void keyboard_init(void);

/* Returns 1 if there is a character waiting in the buffer */
int keyboard_haskey(void);

/* Returns next character from the buffer (blocks if empty... 
 * well, spins — we have no scheduler yet) */
char keyboard_getchar(void);

/* Called directly from the IRQ1 handler in idt.c */
void keyboard_irq_handler(void);