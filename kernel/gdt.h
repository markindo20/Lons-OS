#pragma once
#include <stdint.h>

/*
 * gdt.h — Global Descriptor Table
 *
 * The GDT tells the CPU about memory segments and their permissions.
 * In 64-bit long mode most segmentation is ignored, BUT:
 *   - We still need a valid GDT loaded
 *   - The CS (code segment) selector determines privilege level
 *   - Limine's GDT is temporary — we must install our own
 *
 * Our GDT layout:
 *   Index 0 — Null descriptor       (required by CPU spec)
 *   Index 1 — Kernel Code  (ring 0, execute+read)
 *   Index 2 — Kernel Data  (ring 0, read+write)
 */

/* Segment selector values — used when reloading segment registers
 * Formula: (index * 8) | privilege_level
 * Index 1, ring 0 = 0x08
 * Index 2, ring 0 = 0x10
 */
#define GDT_KERNEL_CODE  0x08
#define GDT_KERNEL_DATA  0x10

/* One GDT entry = 8 bytes */
typedef struct {
    uint16_t limit_low;     /* Bits 0-15 of segment limit */
    uint16_t base_low;      /* Bits 0-15 of base address  */
    uint8_t  base_mid;      /* Bits 16-23 of base address */
    uint8_t  access;        /* Access flags (type, privilege, present) */
    uint8_t  granularity;   /* Limit bits 16-19 + flags   */
    uint8_t  base_high;     /* Bits 24-31 of base address */
} __attribute__((packed)) gdt_entry_t;

/* GDTR — what we load into the CPU with lgdt */
typedef struct {
    uint16_t limit;         /* Size of GDT in bytes - 1 */
    uint64_t base;          /* Virtual address of GDT    */
} __attribute__((packed)) gdtr_t;

/* Initialize and load our GDT */
void gdt_init(void);