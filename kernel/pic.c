/*
 * pic.c — 8259 PIC driver
 */

#include "pic.h"

/*
 * io_wait — Small delay after writing to I/O ports.
 * Old hardware needs time to process commands.
 * Writing to port 0x80 (POST diagnostic port) is the classic trick.
 */
static inline void io_wait(void) {
    __asm__ volatile ("outb %%al, $0x80" : : "a"(0));
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %%al, %%dx" : : "d"(port), "a"(val));
    io_wait();
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %%dx, %%al" : "=a"(val) : "d"(port));
    return val;
}

void pic_init(void) {
    /*
     * ICW1 — Start initialization sequence.
     * 0x11 = ICW4 needed + cascade mode + edge triggered
     */
    outb(PIC_MASTER_CMD,  0x11);
    outb(PIC_SLAVE_CMD,   0x11);

    /* ICW2 — Set vector offsets */
    outb(PIC_MASTER_DATA, IRQ_BASE_MASTER); /* Master: IRQ0 = vector 32 */
    outb(PIC_SLAVE_DATA,  IRQ_BASE_SLAVE);  /* Slave:  IRQ8 = vector 40 */

    /* ICW3 — Tell master/slave about cascade wiring */
    outb(PIC_MASTER_DATA, 0x04); /* Master: slave connected to IRQ2 (bit 2) */
    outb(PIC_SLAVE_DATA,  0x02); /* Slave:  cascade identity = 2            */

    /* ICW4 — 8086 mode */
    outb(PIC_MASTER_DATA, 0x01);
    outb(PIC_SLAVE_DATA,  0x01);

    /*
     * Mask ALL IRQs to start — we'll unmask only what we need.
     * 0xFF = all bits set = all IRQs masked.
     * This prevents spurious interrupts during init.
     */
    outb(PIC_MASTER_DATA, 0xFF);
    outb(PIC_SLAVE_DATA,  0xFF);
}

void pic_send_eoi(uint8_t irq) {
    /* If it came from the slave PIC (IRQ8-15), notify slave too */
    if (irq >= 8) outb(PIC_SLAVE_CMD, PIC_EOI);
    outb(PIC_MASTER_CMD, PIC_EOI);
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC_MASTER_DATA : PIC_SLAVE_DATA;
    if (irq >= 8) irq -= 8;
    uint8_t mask = inb(port) | (uint8_t)(1 << irq);
    outb(port, mask);
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port = (irq < 8) ? PIC_MASTER_DATA : PIC_SLAVE_DATA;
    if (irq >= 8) irq -= 8;
    uint8_t mask = inb(port) & (uint8_t)~(1 << irq);
    outb(port, mask);
}