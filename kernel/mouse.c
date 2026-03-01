// kernel/mouse.c
// PS/2 Mouse Driver with GUI Integration

#include <stdint.h>
#include <stdbool.h>
#include "io.h"
#include "framebuffer.h"
#include "gui.h"

// External IDT function from idt.c
extern void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);

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

// IRQ for mouse (IRQ12, remapped to 44 if PIC remapped)
#define MOUSE_IRQ           12
#define MOUSE_VECTOR        44  // 32 + 12 (if PIC remapped to 0x20)

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

// Screen bounds (set these to your framebuffer resolution)
#define SCREEN_WIDTH        1024
#define SCREEN_HEIGHT       768

// Cursor appearance
#define CURSOR_SIZE         12
static uint32_t cursor_color = 0xFFFFFF; // White cursor

// Forward declarations
static void mouse_handler(void);
static void mouse_wait(uint8_t type);
static void mouse_write(uint8_t data);
static uint8_t mouse_read(void);
static void update_cursor_position(int8_t dx, int8_t dy);
void draw_cursor(void);
void clear_cursor(void);

// Mouse interrupt handler (assembly wrapper calls this)
void mouse_handler_c(void) {
    uint8_t status = inb(MOUSE_STATUS_PORT);
    
    if (!(status & MOUSE_STATUS_OUTPUT_FULL)) {
        // Send EOI
        outb(0xA0, 0x20); // Slave PIC
        outb(0x20, 0x20); // Master PIC
        return;
    }
    
    uint8_t data = inb(MOUSE_DATA_PORT);
    
    // Process mouse packet (3 bytes)
    switch (mouse_cycle) {
        case 0:
            // First byte: buttons and overflow bits
            if ((data & 0x08) == 0x08) { // Bit 3 should be set
                mouse_bytes[0] = data;
                mouse_cycle = 1;
            }
            break;
        case 1:
            // Second byte: X movement
            mouse_bytes[1] = data;
            mouse_cycle = 2;
            break;
        case 2:
            // Third byte: Y movement
            mouse_bytes[2] = data;
            
            // Process complete packet
            uint8_t buttons = mouse_bytes[0] & 0x07;
            int8_t dx = (int8_t)mouse_bytes[1];
            int8_t dy = (int8_t)mouse_bytes[2];
            
            // Sign extension for X
            if (mouse_bytes[0] & 0x10) {
                dx -= 256;
            }
            // Sign extension for Y (inverted)
            if (mouse_bytes[0] & 0x20) {
                dy -= 256;
            }
            
            // Update button state
            mouse_buttons = buttons;
            
            // Update position
            update_cursor_position(dx, -dy); // Y is inverted
            
            mouse_cycle = 0;
            break;
    }
    
    // Send EOI
    outb(0xA0, 0x20); // Slave PIC
    outb(0x20, 0x20); // Master PIC
}

// Assembly wrapper for interrupt handler
__asm__(
    ".global mouse_handler\n"
    "mouse_handler:\n"
    "    pusha\n"
    "    call mouse_handler_c\n"
    "    popa\n"
    "    iretq\n"
);

static void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) {
        // Wait until we can read (output buffer full)
        while (timeout--) {
            if ((inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_OUTPUT_FULL) == 1)
                return;
        }
    } else {
        // Wait until we can write (input buffer empty)
        while (timeout--) {
            if ((inb(MOUSE_STATUS_PORT) & MOUSE_STATUS_INPUT_FULL) == 0)
                return;
        }
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0xD4); // Tell keyboard controller we're sending mouse command
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, data);
}

static uint8_t mouse_read(void) {
    mouse_wait(0);
    return inb(MOUSE_DATA_PORT);
}

static void update_cursor_position(int8_t dx, int8_t dy) {
    // Update position with sensitivity scaling
    mouse_x += dx;
    mouse_y += dy;
    
    // Clamp to screen bounds
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x >= SCREEN_WIDTH - CURSOR_SIZE) mouse_x = SCREEN_WIDTH - CURSOR_SIZE - 1;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y >= SCREEN_HEIGHT - CURSOR_SIZE) mouse_y = SCREEN_HEIGHT - CURSOR_SIZE - 1;
}

// Public function to draw cursor
void mouse_draw_cursor(void) {
    draw_cursor();
}

// Public function to erase cursor (just redraws background)
void mouse_erase_cursor(void) {
    clear_cursor();
}

// Check if mouse state changed (for polling mode)
bool mouse_poll(void) {
    // Check if position or buttons changed
    if (mouse_x != prev_mouse_x || mouse_y != prev_mouse_y || mouse_buttons != prev_buttons) {
        // Update previous state
        prev_mouse_x = mouse_x;
        prev_mouse_y = mouse_y;
        prev_buttons = mouse_buttons;
        return true;
    }
    return false;
}

void draw_cursor(void) {
    // Simple arrow cursor drawing
    // Draw arrow shape
    for (int i = 0; i < CURSOR_SIZE; i++) {
        // Main diagonal
        fb_putpixel(mouse_x + i, mouse_y + i, cursor_color);
        // Top edge
        if (i < CURSOR_SIZE / 2) {
            fb_putpixel(mouse_x + i, mouse_y, cursor_color);
        }
        // Bottom edge
        if (i < CURSOR_SIZE * 2 / 3) {
            fb_putpixel(mouse_x, mouse_y + i, cursor_color);
        }
    }
    
    // Fill the arrow slightly for visibility
    for (int y = 1; y < CURSOR_SIZE - 2; y++) {
        for (int x = 1; x < y; x++) {
            if (x + y < CURSOR_SIZE) {
                fb_putpixel(mouse_x + x, mouse_y + y, 0xCCCCCC); // Light gray fill
            }
        }
    }
}

void clear_cursor(void) {
    // Redraw the area where cursor was
    gui_redraw_area(mouse_x, mouse_y, CURSOR_SIZE, CURSOR_SIZE);
}

// Initialize mouse
void mouse_init(void) {
    // Wait for keyboard controller
    mouse_wait(1);
    
    // Enable mouse auxiliary device
    outb(MOUSE_COMMAND_PORT, MOUSE_CMD_ENABLE);
    
    // Enable interrupts
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0x20); // Read command byte
    mouse_wait(0);
    uint8_t status = inb(MOUSE_DATA_PORT) | 2; // Enable IRQ12
    mouse_wait(1);
    outb(MOUSE_COMMAND_PORT, 0x60); // Write command byte
    mouse_wait(1);
    outb(MOUSE_DATA_PORT, status);
    
    // Reset mouse to defaults
    mouse_write(MOUSE_CMD_DEFAULT);
    mouse_read(); // Acknowledge
    
    // Enable streaming
    mouse_write(MOUSE_CMD_ENABLE_STREAMING);
    mouse_read(); // Acknowledge
    
    // Set initial position to center of screen
    mouse_x = SCREEN_WIDTH / 2;
    mouse_y = SCREEN_HEIGHT / 2;
    
    // Register interrupt handler using idt_set_gate from idt.c
    idt_set_gate(MOUSE_VECTOR, (uint64_t)mouse_handler, 0x08, 0x8E);
    
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