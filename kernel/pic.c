// kernel/pic.c
// Programmable Interrupt Controller (8259) driver – FIXED

#include <stdint.h>
#include "io.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1

#define ICW1_INIT     0x10
#define ICW1_ICW4     0x01
#define ICW4_8086     0x01

// Initialize both PICs and remap IRQs to vectors 32-47
void pic_init(void) {
    uint8_t a1, a2;

    a1 = inb(PIC1_DATA);   // save masks
    a2 = inb(PIC2_DATA);

    // Start initialization sequence (cascade mode)
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    // ICW2: vector offsets
    outb(PIC1_DATA, 0x20);  // master offset 32
    outb(PIC2_DATA, 0x28);  // slave offset 40

    // ICW3: tell master PIC that there is a slave at IRQ2 (bit 2)
    outb(PIC1_DATA, 1 << 2);
    outb(PIC2_DATA, 2);     // tell slave its cascade identity

    // ICW4: 8086 mode
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // Mask ALL IRQs initially — drivers will unmask what they need
    // But keep IRQ2 (cascade) unmasked on master so slave PIC works!
    outb(PIC1_DATA, 0xFB);  // 1111 1011 — all masked except IRQ2 (cascade)
    outb(PIC2_DATA, 0xFF);  // 1111 1111 — all masked
}

// Send End-Of-Interrupt to PIC(s)
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8)
        outb(PIC2_COMMAND, 0x20);
    outb(PIC1_COMMAND, 0x20);
}

// Unmask a specific IRQ
void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = inb(port);
    mask &= ~(1 << irq);
    outb(port, mask);
}

// Mask a specific IRQ
void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t mask;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    mask = inb(port);
    mask |= (1 << irq);
    outb(port, mask);
}