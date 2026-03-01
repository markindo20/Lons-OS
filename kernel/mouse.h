// kernel/mouse.h
#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>
#include <stdbool.h>

// Mouse button constants
#define MOUSE_LEFT      0x01
#define MOUSE_RIGHT     0x02
#define MOUSE_MIDDLE    0x04

// External mouse state (variables, not functions!)
extern int32_t mouse_x;
extern int32_t mouse_y;
extern uint8_t mouse_buttons;

// Functions
void mouse_init(void);
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);
bool mouse_is_ready(void);

// Cursor drawing functions
void mouse_draw_cursor(void);
void mouse_erase_cursor(void);

// Polling function for non-interrupt mode
bool mouse_poll(void);

#endif // MOUSE_H