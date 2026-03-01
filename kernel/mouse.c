/*
 * mouse.c — PS/2 Mouse Driver
 *
 * Initializes the PS/2 mouse via the 8042 keyboard controller,
 * handles IRQ12, parses 3-byte packets, maintains cursor position,
 * and draws/erases a cursor sprite on the framebuffer.
 */

#include "mouse.h"
#include "pic.h"
#include "framebuffer.h"
#include "idt.h"

/* ── I/O port helpers ── */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port) : "memory");
    return val;
}
static inline void io_wait(void) { outb(0x80, 0); }

/* ── 8042 PS/2 Controller ports ── */
#define PS2_DATA    0x60   /* Read/write data                    */
#define PS2_STATUS  0x64   /* Read status                        */
#define PS2_CMD     0x64   /* Write command                      */

/* Status register bits */
#define PS2_OUT_FULL  (1 << 0)   /* Output buffer has data to read  */
#define PS2_IN_FULL   (1 << 1)   /* Input buffer has data (CPU busy) */
#define PS2_MOUSE_BIT (1 << 5)   /* Incoming byte is from mouse      */

/* ── Wait helpers ── */
static void ps2_wait_write(void) {
    /* Wait until the controller is ready to accept a byte */
    int timeout = 100000;
    while ((inb(PS2_STATUS) & PS2_IN_FULL) && timeout--);
}
static void ps2_wait_read(void) {
    /* Wait until there is data to read */
    int timeout = 100000;
    while (!(inb(PS2_STATUS) & PS2_OUT_FULL) && timeout--);
}

/* Send a command to the 8042 controller */
static void ps2_cmd(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_CMD, cmd);
}

/* Send a byte to the MOUSE (not keyboard) via the controller */
static void mouse_write(uint8_t byte) {
    ps2_cmd(0xD4);          /* Tell controller: next byte goes to mouse */
    ps2_wait_write();
    outb(PS2_DATA, byte);
}

/* Read a byte from the PS/2 data port */
static uint8_t ps2_read(void) {
    ps2_wait_read();
    return inb(PS2_DATA);
}

/* ── Mouse state ── */
static int32_t  cur_x       = 0;
static int32_t  cur_y       = 0;
static uint8_t  cur_buttons = 0;
static int      mouse_dirty = 0;   /* 1 = new event pending */

/* Packet assembly — mouse sends 3 bytes per event */
static uint8_t packet[3];
static int     packet_idx = 0;

/* ─────────────────────────────────────────────
 * Cursor sprite
 *
 * 12x19 pixel arrow cursor drawn with 3 colors:
 *   White fill, black outline, transparent (skip)
 *
 * Each row is a string: 'W'=white, 'B'=black, ' '=transparent
 * ───────────────────────────────────────────── */
#define CUR_W 12
#define CUR_H 19

static const char *cursor_sprite[CUR_H] = {
    "B           ",
    "BB          ",
    "BWB         ",
    "BWWB        ",
    "BWWWB       ",
    "BWWWWB      ",
    "BWWWWWB     ",
    "BWWWWWWB    ",
    "BWWWWWWWB   ",
    "BWWWWWWWWB  ",
    "BWWWWWWB    ",
    "BWWBWWB     ",
    "BWB BWWB    ",
    "BB   BWWB   ",
    "B     BWWB  ",
    "       BWWB ",
    "        BWWB",
    "         BWB",
    "          B ",
};

/* Saved pixels under the cursor (to restore on erase) */
static uint32_t cursor_save[CUR_H][CUR_W];
static int32_t  cursor_drawn_x = -1;
static int32_t  cursor_drawn_y = -1;

/* ─────────────────────────────────────────────
 * mouse_erase_cursor
 * ───────────────────────────────────────────── */
void mouse_erase_cursor(void) {
    if (cursor_drawn_x < 0) return;

    for (int row = 0; row < CUR_H; row++) {
        for (int col = 0; col < CUR_W; col++) {
            int sx = cursor_drawn_x + col;
            int sy = cursor_drawn_y + row;
            if (sx < 0 || sy < 0 ||
                (uint32_t)sx >= g_fb.width ||
                (uint32_t)sy >= g_fb.height) continue;
            if (cursor_sprite[row][col] != ' ') {
                fb_put_pixel((uint32_t)sx, (uint32_t)sy,
                             cursor_save[row][col]);
            }
        }
    }
    cursor_drawn_x = -1;
    cursor_drawn_y = -1;
}

/* ─────────────────────────────────────────────
 * mouse_draw_cursor
 * ───────────────────────────────────────────── */
void mouse_draw_cursor(void) {
    int32_t sx = cur_x;
    int32_t sy = cur_y;

    /* Save pixels underneath */
    for (int row = 0; row < CUR_H; row++) {
        for (int col = 0; col < CUR_W; col++) {
            int px = sx + col;
            int py = sy + row;
            if (px < 0 || py < 0 ||
                (uint32_t)px >= g_fb.width ||
                (uint32_t)py >= g_fb.height) {
                cursor_save[row][col] = 0;
                continue;
            }
            if (cursor_sprite[row][col] != ' ') {
                cursor_save[row][col] =
                    g_fb.addr[(uint32_t)py * (g_fb.pitch / 4) + (uint32_t)px];
            }
        }
    }

    /* Draw cursor */
    for (int row = 0; row < CUR_H; row++) {
        for (int col = 0; col < CUR_W; col++) {
            int px = sx + col;
            int py = sy + row;
            if (px < 0 || py < 0 ||
                (uint32_t)px >= g_fb.width ||
                (uint32_t)py >= g_fb.height) continue;

            char p = cursor_sprite[row][col];
            if (p == 'B')
                fb_put_pixel((uint32_t)px, (uint32_t)py, 0x00000000);
            else if (p == 'W')
                fb_put_pixel((uint32_t)px, (uint32_t)py, 0x00FFFFFF);
            /* ' ' = transparent, skip */
        }
    }

    cursor_drawn_x = sx;
    cursor_drawn_y = sy;
}

/* ─────────────────────────────────────────────
 * mouse_irq_handler — called on every IRQ12
 * ───────────────────────────────────────────── */
void mouse_irq_handler(void) {
    uint8_t byte = inb(PS2_DATA);

    if (packet_idx == 0) {
        /*
         * Sync: byte 0 must have bit 3 set.
         * If it doesn't, we're out of sync — discard and wait
         * for a valid packet start.
         */
        if (!(byte & 0x08)) {
            pic_send_eoi(12);
            return;
        }
    }

    packet[packet_idx++] = byte;

    if (packet_idx == 3) {
        packet_idx = 0;

        uint8_t flags = packet[0];

        /* Overflow — discard corrupt packet */
        if ((flags & 0x40) || (flags & 0x80)) {
            pic_send_eoi(12);
            return;
        }

        /* Delta X — 9-bit signed (sign bit in flags byte 4) */
        int32_t dx = (int32_t)packet[1];
        if (flags & 0x10) dx |= 0xFFFFFF00;  /* Sign extend */

        /* Delta Y — 9-bit signed (sign bit in flags byte 5), INVERTED */
        int32_t dy = (int32_t)packet[2];
        if (flags & 0x20) dy |= 0xFFFFFF00;  /* Sign extend */
        dy = -dy;  /* Flip: PS/2 Y+ = up, screen Y+ = down */

        /* Update position — clamp to screen */
        cur_x += dx;
        cur_y += dy;

        if (cur_x < 0) cur_x = 0;
        if (cur_y < 0) cur_y = 0;
        if ((uint32_t)cur_x >= g_fb.width)  cur_x = (int32_t)(g_fb.width  - 1);
        if ((uint32_t)cur_y >= g_fb.height) cur_y = (int32_t)(g_fb.height - 1);

        /* Button state */
        cur_buttons = flags & 0x07;

        mouse_dirty = 1;
    }

    pic_send_eoi(12);
}

/* ─────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────── */
int32_t mouse_x(void)       { return cur_x; }
int32_t mouse_y(void)       { return cur_y; }
uint8_t mouse_buttons(void) { return cur_buttons; }

int mouse_poll(void) {
    if (mouse_dirty) { mouse_dirty = 0; return 1; }
    return 0;
}

/* ─────────────────────────────────────────────
 * IRQ12 stub — asm trampoline into mouse_irq_handler
 * ───────────────────────────────────────────── */
__asm__ (
    ".global mouse_irq_stub\n"
    "mouse_irq_stub:\n"
    "    push %rax\n"
    "    push %rcx\n"
    "    push %rdx\n"
    "    push %rsi\n"
    "    push %rdi\n"
    "    push %r8\n"
    "    push %r9\n"
    "    push %r10\n"
    "    push %r11\n"
    "    call mouse_irq_handler\n"
    "    pop  %r11\n"
    "    pop  %r10\n"
    "    pop  %r9\n"
    "    pop  %r8\n"
    "    pop  %rdi\n"
    "    pop  %rsi\n"
    "    pop  %rdx\n"
    "    pop  %rcx\n"
    "    pop  %rax\n"
    "    iretq\n"
);
extern void mouse_irq_stub(void);

/* Install IRQ gate helper — same pattern as keyboard.c */
#define GDT_KERNEL_CODE 0x08
static void idt_set_irq_gate(uint8_t vector, void *handler) {
    uint64_t addr = (uint64_t)handler;
    idt[vector].offset_low  = (uint16_t)(addr & 0xFFFF);
    idt[vector].selector    = GDT_KERNEL_CODE;
    idt[vector].ist         = 0;
    idt[vector].type_attr   = 0x8E;
    idt[vector].offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    idt[vector].reserved    = 0;
}

/* ─────────────────────────────────────────────
 * mouse_init
 * ───────────────────────────────────────────── */
void mouse_init(void) {
    /* Start cursor in center of screen */
    cur_x = (int32_t)(g_fb.width  / 2);
    cur_y = (int32_t)(g_fb.height / 2);

    /*
     * Enable the PS/2 mouse port on the 8042 controller.
     *
     * The 8042 has two ports: keyboard (port 1) and mouse (port 2).
     * By default port 2 (mouse) is disabled. We enable it by:
     *   1. Sending command 0xA8 ("enable mouse port")
     *   2. Reading the current config byte
     *   3. Setting bit 1 (enable mouse IRQ12) in the config
     *   4. Writing the config byte back
     *   5. Sending 0xF4 to the mouse ("enable data reporting")
     */

    /* Step 1: Enable auxiliary (mouse) port */
    ps2_cmd(0xA8);
    io_wait();

    /* Step 2: Read current controller config */
    ps2_cmd(0x20);
    uint8_t config = ps2_read();

    /* Step 3: Enable mouse IRQ (bit 1) and clear mouse disable bit (bit 5) */
    config |=  (1 << 1);  /* Enable IRQ12 */
    config &= ~(1 << 5);  /* Clear "mouse disabled" bit */

    /* Step 4: Write config back */
    ps2_cmd(0x60);
    ps2_wait_write();
    outb(PS2_DATA, config);
    io_wait();

    /* Step 5: Send 0xF4 to mouse — "start sending packets" */
    mouse_write(0xF4);
    ps2_read();  /* Discard ACK byte (0xFA) */

    /* Install IRQ12 handler at vector 0x2C (PIC2_OFFSET + 4 = 44) */
    idt_set_irq_gate(0x2C, (void *)mouse_irq_stub);

    /* Unmask IRQ12 on the slave PIC */
    pic_unmask_irq(12);
}