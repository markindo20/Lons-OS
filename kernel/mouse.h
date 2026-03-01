#pragma once
#include <stdint.h>

/*
 * mouse.h — PS/2 Mouse Driver
 *
 * The PS/2 mouse uses the same 8042 controller as the keyboard.
 * It sends data on IRQ12 (vector 0x2C = PIC2_OFFSET + 4).
 *
 * Each mouse event is a 3-byte packet:
 *
 *   Byte 0: flags
 *     Bit 0 : Left button pressed
 *     Bit 1 : Right button pressed
 *     Bit 2 : Middle button pressed
 *     Bit 3 : Always 1 (sync bit — lets us detect packet start)
 *     Bit 4 : X sign (1 = negative delta)
 *     Bit 5 : Y sign (1 = negative delta)
 *     Bit 6 : X overflow
 *     Bit 7 : Y overflow
 *
 *   Byte 1: X movement delta (signed, 2's complement with sign in byte 0)
 *   Byte 2: Y movement delta (signed, 2's complement with sign in byte 0)
 *
 * NOTE: PS/2 Y axis is INVERTED — positive delta means UP on screen,
 * but screen Y increases downward. We flip it.
 */

/* Mouse button bitmask (returned by mouse_buttons()) */
#define MOUSE_LEFT   (1 << 0)
#define MOUSE_RIGHT  (1 << 1)
#define MOUSE_MIDDLE (1 << 2)

/*
 * mouse_init — Initialize PS/2 mouse.
 * Enables the mouse port on the 8042 controller,
 * sets mouse to stream mode, and installs IRQ12 handler.
 * Must be called after pic_init() and idt_init().
 */
void mouse_init(void);

/* Current cursor position (clamped to screen bounds) */
int32_t  mouse_x(void);
int32_t  mouse_y(void);

/* Current button state — bitmask of MOUSE_LEFT/RIGHT/MIDDLE */
uint8_t  mouse_buttons(void);

/*
 * mouse_poll — Returns 1 if a new mouse event arrived since
 * the last call. Clears the flag when called.
 * Use in the main loop to know when to redraw the cursor.
 */
int mouse_poll(void);

/*
 * mouse_draw_cursor — Draw the cursor sprite at current position.
 * Saves the pixels underneath so we can restore them later.
 */
void mouse_draw_cursor(void);

/*
 * mouse_erase_cursor — Restore the pixels under the cursor.
 * Call this BEFORE moving the cursor or redrawing anything under it.
 */
void mouse_erase_cursor(void);