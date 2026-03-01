// kernel/keyboard.c
// PS/2 Keyboard Driver with Circular Buffer – FIXED (no EOI needed here, idt.c handles it)

#include <stdint.h>
#include <stdbool.h>
#include "io.h"
#include "gui.h"
#include "framebuffer.h"

#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

#define BUFFER_SIZE 128

static char kbd_buffer[BUFFER_SIZE];
static int  kbd_buffer_head = 0;
static int  kbd_buffer_tail = 0;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool alt_pressed = false;

// Simple US layout scancode to ASCII (set 1)
static const char scancode_to_ascii[] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,   'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0,   ' ', 0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char scancode_to_ascii_shift[] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,   'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0,   ' ', 0, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0
};

void kbd_init(void) {
    kbd_buffer_head = kbd_buffer_tail = 0;
}

static void kbd_push_char(char c) {
    int next = (kbd_buffer_head + 1) % BUFFER_SIZE;
    if (next != kbd_buffer_tail) {
        kbd_buffer[kbd_buffer_head] = c;
        kbd_buffer_head = next;
    }
}

bool kbd_haschar(void) {
    return kbd_buffer_head != kbd_buffer_tail;
}

char kbd_getchar(void) {
    if (!kbd_haschar()) return 0;
    char c = kbd_buffer[kbd_buffer_tail];
    kbd_buffer_tail = (kbd_buffer_tail + 1) % BUFFER_SIZE;
    return c;
}

void kbd_irq_handler(void) {
    uint8_t status = inb(KEYBOARD_STATUS_PORT);
    if (!(status & 0x01)) return;

    uint8_t scancode = inb(KEYBOARD_DATA_PORT);

    bool released = scancode & 0x80;
    scancode &= 0x7F;

    // Handle modifier keys
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = !released;
        return;
    }
    if (scancode == 0x1D) {
        ctrl_pressed = !released;
        return;
    }
    if (scancode == 0x38) {
        alt_pressed = !released;
        return;
    }

    if (released) return;

    char c;
    if (shift_pressed) {
        c = scancode_to_ascii_shift[scancode];
    } else {
        c = scancode_to_ascii[scancode];
    }

    if (c == 0) return;

    kbd_push_char(c);

    // EOI is sent by irq_handler() in idt.c — do NOT send it here
}