#ifndef KERNEL_IDT_H
#define KERNEL_IDT_H

#include <stdint.h>

// IDT entry structure (matching your idt.c)
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

// IDT register structure
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idtr_t;

// Interrupt frame structure (used by handlers)
typedef struct {
    uint64_t rax, rbx, rcx, rdx, rsi, rdi, rbp;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t vector, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} interrupt_frame_t;

// Public function to set an IDT gate (used by drivers)
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);

// Initialize IDT (called from kernel main)
void idt_init(void);

#endif