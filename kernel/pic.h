#pragma once
#include <stdint.h>

/*
 * pic.h — Intel 8259 Programmable Interrupt Controller
 *
 * Two cascaded 8259 PICs handle hardware IRQs.
 * Default mapping collides with CPU exception vectors — we remap:
 *
 *   IRQ0-7  → vectors 32-39  (master)
 *   IRQ8-15 → vectors 40-47  (slave)
 *
 * Key IRQs:
 *   IRQ0  (vec 32) — PIT Timer
 *   IRQ1  (vec 33) — PS/2 Keyboard  ← we use this
 *   IRQ12 (vec 44) — PS/2 Mouse     ← later
 */

#define PIC_MASTER_CMD   0x20
#define PIC_MASTER_DATA  0x21
#define PIC_SLAVE_CMD    0xA0
#define PIC_SLAVE_DATA   0xA1
#define PIC_EOI          0x20

#define IRQ_BASE_MASTER  32
#define IRQ_BASE_SLAVE   40

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_mask_irq(uint8_t irq);
void pic_unmask_irq(uint8_t irq);