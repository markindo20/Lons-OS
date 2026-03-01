// kernel/mouse.c
// PS/2 Mouse Driver with GUI Integration – FIXED

#include <stdint.h>
#include <stdbool.h>
#include "io.h"
#include "framebuffer.h"
#include "gui.h"
#include "idt.h"

// Debug: interrupt counter – visible from kernel.c
volatile uint64_t mouse_interrupt_count = 0;

// Mouse constants
#define MOUSE_DATA_PORT     0x60
#define MOUSE_STATUS_PORT   0x64
#define MOUSE_COMMAND_PORT  0x64

#define MOUSE_CMD_ENABLE    0xA8
#define MOUSE_CMD_DISABLE   0xA7
#define MOUSE_CMD_READ      0x20
#define MOUSE_CMD_WRITE     0x60
#define MOUSE_CMD_DEFAULT   0xF6
#define MOUSE_CMD_ENABLE_STREAMING 0xF4

#define MOUSE_STATUS_OUTPUT_FULL 0x01
#define MOUSE_STATUS_INPUT_FULL  0x02

// Mouse packet bytes
static uint8_t mouse_cycle = 0;
static int8_t mouse_bytes[3];
static bool mouse_ready = false;

// Cursor position (global for GUI access)
int32_t mouse_x = 0;
int32_t mouse_y = 0;
uint8_t mouse_buttons = 0;

// Previous state for polling
static uint8_t prev_buttons = 0;
static int32_t prev_mouse_x = 0;
static int32_t prev_mouse_y = 0;

// Cursor appearance
#define CURSOR_SIZE 12
static uint32_t cursor_color = 0xFFFFFF; // White cursor

// Save buffer for pixels under cursor
static uint32_t cursor_save[CURSOR_SIZE * CURSOR_SIZE];
static int32_t  save_x = 0, save_y = 0;
static bool     cursor_saved = false;

// Forward declarations
static void mouse_wait(uint8_t type);
static void mouse_write(uint8_t data);
static uint8_t mouse_read(void);
static void update_cursor_position(int8_t dx, int8_t dy);
void draw_cursor(void);
void clear_cursor(void);

// Mouse interrupt handler — called from irq_handler() in idt.c
void mouse_handler_c(void) {
    // Increment debug counter
    mouse_interrupt_count++;

    uint8_t status = inb(MOUSE_STATUS_PORT);

    if (!(status & MOUSE_STATUS_OUTPUT_FULL)) {
        // No data ready, just return — EOI is sent by irq_handler() in idt.c
        return;
    }

    uint8_t data = inb(MOUSE_DATA_PORT);

    // Process mouse packet (3 bytes)
    switch (mouse_cycle) {
        case 0:
            // First byte: buttons and overflow bits
            if ((data & 0x08) == 0x08) { // Bit 3 should always be set
                mouse_bytes[0] = data;
                mouse_cycle = 1;
            }
            break;
        case 1:
            // Second byte: X movement
            mouse_bytes[1] = data;
            mouse_cycle = 2;
            break;
        case 2: {
            // Third byte: Y movement
            mouse_bytes[2] = data;

            // Process complete packet
            int16_t dx = (int16_t)((int8_t)mouse_bytes[1]);
            int16_t dy = (int16_t)((int8_t)mouse_bytes[2]);

            // Sign extension for X
            if (mouse_bytes[0] & 0x10) {
                dx = dx - 256;
            }
            // Sign extension for Y
            if (mouse_bytes[0] & 0x20) {
                dy = dy - 256;
            }

            // Check overflow bits — discard packet if overflow
            if (!(mouse_bytes[0] & 0x40) && !(mouse_bytes[0] & 0x80)) {
                // Update button state
                mouse_buttons = mouse_bytes[0] & 0x07;

                // Update position (Y is inverted in PS/2 protocol)
                update_cursor_position((int8_t)dx, (int8_t)(-dy));
            }

            mouse_cycle = 0;
            break;
        }
    }

    // EOI is sent by irq_handler() in idt.c — do NOT send it here
}

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        // Wait until we can read (output buffer full)
        while (timeout--) {
            if (inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_OUTPUT_FULL)
                return;
        }
    } else {
        // Wait until we can write (input buffer empty)
        while (timeout--) {
            if (!(inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_INPUT_FULL))
                return;
        }
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0xD4); // Tell controller: next byte goes to mouse
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, data);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_DATA_PORT);
}

static void update_cursor_position(int8_t dx, int8_t dy) {
    mouse_x += dx;
    mouse_y += dy;

    // Clamp to actual framebuffer bounds
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= (int32_t)g_fb.width - CURSOR_SIZE)
        mouse_x = (int32_t)g_fb.width - CURSOR_SIZE - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= (int32_t)g_fb.height - CURSOR_SIZE)
        mouse_y = (int32_t)g_fb.height - CURSOR_SIZE - 1;
}

// Save the pixels underneath the cursor, then draw cursor on top
void draw_cursor(void) {
    save_x = mouse_x;
    save_y = mouse_y;
    uint32_t rpx = g_fb.pitch / 4;

    // Save pixels under cursor area
    for (int y = 0; y < CURSOR_SIZE; y++) {
        for (int x = 0; x < CURSOR_SIZE; x++) {
            uint32_t px = (uint32_t)(mouse_x + x);
            uint32_t py = (uint32_t)(mouse_y + y);
            if (px < g_fb.width && py < g_fb.height) {
                cursor_save[y * CURSOR_SIZE + x] = g_fb.addr[py * rpx + px];
            } else {
                cursor_save[y * CURSOR_SIZE + x] = 0;
            }
        }
    }
    cursor_saved = true;

    // Draw arrow shape
    for (int i = 0; i < CURSOR_SIZE; i++) {
        fb_put_pixel(mouse_x + i, mouse_y + i, cursor_color);
        if (i < CURSOR_SIZE / 2) {
            fb_put_pixel(mouse_x + i, mouse_y, cursor_color);
        }
        if (i < CURSOR_SIZE * 2 / 3) {
            fb_put_pixel(mouse_x, mouse_y + i, cursor_color);
        }
    }

    // Fill the arrow slightly for visibility
    for (int y = 1; y < CURSOR_SIZE - 2; y++) {
        for (int x = 1; x < y; x++) {
            if (x + y < CURSOR_SIZE) {
                fb_put_pixel(mouse_x + x, mouse_y + y, 0xCCCCCC);
            }
        }
    }
}

// Restore the saved pixels (erase cursor without destroying window content)
void clear_cursor(void) {
    if (!cursor_saved) return;

    uint32_t rpx = g_fb.pitch / 4;

    for (int y = 0; y < CURSOR_SIZE; y++) {
        for (int x = 0; x < CURSOR_SIZE; x++) {
            uint32_t px = (uint32_t)(save_x + x);
            uint32_t py = (uint32_t)(save_y + y);
            if (px < g_fb.width && py < g_fb.height) {
                g_fb.addr[py * rpx + px] = cursor_save[y * CURSOR_SIZE + x];
            }
        }
    }
    cursor_saved = false;
}

// Public wrappers
void mouse_draw_cursor(void) {
    draw_cursor();
}

void mouse_erase_cursor(void) {
    clear_cursor();
}

// Check if mouse state changed (for polling mode)
bool mouse_poll(void) {
    if (mouse_x != prev_mouse_x || mouse_y != prev_mouse_y || mouse_buttons != prev_buttons) {
        prev_mouse_x = mouse_x;
        prev_mouse_y = mouse_y;
        prev_buttons = mouse_buttons;
        return true;
    }
    return false;
}

// Initialize mouse
void mouse_init(void) {
    // Wait for keyboard controller
    mouse_wait(1);

    // Enable mouse auxiliary device
    outb(MOUSE_COMMAND_PORT, MOUSE_CMD_ENABLE);

    // Small delay
    for (volatile int i = 0; i < 10000; i++);

    // Enable IRQ12 in the PS/2 controller configuration byte
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0x20); // Read command byte
    mouse_wait(0);
    uint8_t status = inb(MOUSE_DATA_PORT);
    status |= 2;    // Enable IRQ12 (auxiliary interrupt enable)
    status &= ~0x20; // Make sure auxiliary clock is NOT disabled
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0x60); // Write command byte
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, status);

    // Reset mouse to defaults
    mouse_write(MOUSE_CMD_DEFAULT);
    mouse_read(); // Acknowledge

    // Enable data streaming
    mouse_write(MOUSE_CMD_ENABLE_STREAMING);
    mouse_read(); // Acknowledge

    // Set initial position to center of actual screen
    mouse_x = (int32_t)g_fb.width / 2;
    mouse_y = (int32_t)g_fb.height / 2;

    // Do NOT overwrite IDT vector 44 here.
    // idt_init() already registered irq_stub_44 which routes to irq_handler() → mouse_handler_c().
    // Overwriting it with a separate asm stub caused inconsistent EOI handling.

    mouse_ready = true;

    // Initial draw
    draw_cursor();
}

// Get current mouse state
void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}

// Check if mouse is ready
bool mouse_is_ready(void) {
    return mouse_ready;
}