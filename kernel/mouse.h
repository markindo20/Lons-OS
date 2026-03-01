// kernel/mouse.h
#ifndef KERNEL_MOUSE_H
#define KERNEL_MOUSE_H

#include <stdint.h>
#include <stdbool.h>

#define MOUSE_LEFT   0x01
#define MOUSE_RIGHT  0x02
#define MOUSE_MIDDLE 0x04

extern int32_t mouse_x;
extern int32_t mouse_y;
extern uint8_t mouse_buttons;

void mouse_init(void);
void mouse_handler_c(void);
bool mouse_is_ready(void);
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons);
void mouse_draw_cursor(void);
void mouse_erase_cursor(void);
bool mouse_poll(void);

#endif