/*
 * keyboard.c — PS/2 Keyboard Driver
 *
 * Handles IRQ1 (vector 33), reads scan codes from port 0x60,
 * converts to ASCII using a US QWERTY layout table,
 * and stores printable characters in a ring buffer.
 */

#include "keyboard.h"
#include "pic.h"

/* PS/2 data port — read scan codes here */
#define KB_DATA_PORT 0x60

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(val) : "d"(port));
    return val;
}

/* ─────────────────────────────────────────────
 * US QWERTY Scancode Set 1 → ASCII tables
 *
 * Index = scancode (0x00 - 0x58)
 * Value = ASCII character (0 = non-printable/ignore)
 *
 * Two tables: unshifted and shifted (caps/shift held)
 * ───────────────────────────────────────────── */
static const char scancode_table[128] = {
/*00*/  0,   0,  '1', '2', '3', '4', '5', '6',
/*08*/ '7', '8', '9', '0', '-', '=',  '\b', '\t',
/*10*/ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i',
/*18*/ 'o', 'p', '[', ']', '\n',  0,  'a', 's',
/*20*/ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';',
/*28*/ '\'','`',   0, '\\','z', 'x', 'c', 'v',
/*30*/ 'b', 'n', 'm', ',', '.', '/',   0,  '*',
/*38*/  0,  ' ',   0,   0,   0,   0,   0,   0,
/*40*/  0,   0,   0,   0,   0,   0,   0,  '7',
/*48*/ '8', '9', '-', '4', '5', '6', '+', '1',
/*50*/ '2', '3', '0', '.',   0,   0,   0,   0,
/*58*/  0
};

static const char scancode_table_shift[128] = {
/*00*/  0,   0,  '!', '@', '#', '$', '%', '^',
/*08*/ '&', '*', '(', ')', '_', '+', '\b', '\t',
/*10*/ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I',
/*18*/ 'O', 'P', '{', '}', '\n',  0,  'A', 'S',
/*20*/ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':',
/*28*/ '"', '~',   0,  '|', 'Z', 'X', 'C', 'V',
/*30*/ 'B', 'N', 'M', '<', '>', '?',   0,  '*',
/*38*/  0,  ' ',   0,   0,   0,   0,   0,   0,
/*40*/  0,   0,   0,   0,   0,   0,   0,  '7',
/*48*/ '8', '9', '-', '4', '5', '6', '+', '1',
/*50*/ '2', '3', '0', '.',   0,   0,   0,   0,
/*58*/  0
};

/* Scancodes for modifier keys */
#define SC_LSHIFT_PRESS    0x2A
#define SC_RSHIFT_PRESS    0x36
#define SC_LSHIFT_RELEASE  0xAA
#define SC_RSHIFT_RELEASE  0xB6
#define SC_CAPS_LOCK       0x3A
#define SC_RELEASE_BIT     0x80  /* Bit 7 set = key release */

/* Modifier state */
static int shift_held = 0;
static int caps_lock  = 0;

/* ── Ring buffer ── */
static char     kb_buf[KEYBOARD_BUFFER_SIZE];
static uint32_t kb_read  = 0;  /* Read index  */
static uint32_t kb_write = 0;  /* Write index */

static void kb_buf_push(char c) {
    uint32_t next = (kb_write + 1) % KEYBOARD_BUFFER_SIZE;
    if (next == kb_read) return; /* Buffer full — drop keystroke */
    kb_buf[kb_write] = c;
    kb_write = next;
}

static char kb_buf_pop(void) {
    if (kb_read == kb_write) return 0; /* Empty */
    char c = kb_buf[kb_read];
    kb_read = (kb_read + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

/* ─────────────────────────────────────────────
 * kbd_irq_handler — called from IDT on IRQ1
 * ───────────────────────────────────────────── */
void kbd_irq_handler(void) {
    uint8_t sc = inb(KB_DATA_PORT);

    /* Track shift/caps state */
    if (sc == SC_LSHIFT_PRESS  || sc == SC_RSHIFT_PRESS)  { shift_held = 1; goto done; }
    if (sc == SC_LSHIFT_RELEASE|| sc == SC_RSHIFT_RELEASE) { shift_held = 0; goto done; }
    if (sc == SC_CAPS_LOCK) { caps_lock = !caps_lock; goto done; }

    /* Ignore key-release events (bit 7 set) */
    if (sc & SC_RELEASE_BIT) goto done;

    /* Ignore out-of-range */
    if (sc >= 128) goto done;

    /* Determine if we should use shifted table */
    int use_shift = shift_held ^ caps_lock;
    char c = use_shift ? scancode_table_shift[sc] : scancode_table[sc];

    if (c) kb_buf_push(c);

done:
    pic_send_eoi(1); /* IRQ1 — always acknowledge */
}

/* ─────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────── */
void kbd_init(void) {
    /* Unmask IRQ1 in the PIC — keyboard interrupts now flow */
    pic_unmask_irq(1);
}

int kbd_haschar(void) {
    return kb_read != kb_write;
}

char kbd_getchar(void) {
    while (!kbd_haschar()) {
        __asm__ volatile ("hlt"); /* Wait for next interrupt */
    }
    return kb_buf_pop();
}