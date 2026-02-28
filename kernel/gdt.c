/*
 * gdt.c — Global Descriptor Table setup
 *
 * In 64-bit mode the CPU mostly ignores base/limit in descriptors,
 * but the ACCESS byte and the LONG MODE flag in granularity still matter.
 */

#include "gdt.h"

/* Our 3-entry GDT stored in the kernel's .data section */
static gdt_entry_t gdt[3];
static gdtr_t      gdtr;

/*
 * make_entry — Build one GDT descriptor
 *
 * In 64-bit long mode:
 *   base  = 0  (ignored by CPU in 64-bit mode)
 *   limit = 0  (ignored in 64-bit mode)
 *
 * What matters is the ACCESS byte and GRANULARITY flags.
 *
 * ACCESS byte breakdown:
 *   Bit 7: Present       (1 = descriptor is valid)
 *   Bit 6-5: DPL         (0 = ring 0 / kernel)
 *   Bit 4: Descriptor    (1 = code/data, 0 = system)
 *   Bit 3: Executable    (1 = code segment)
 *   Bit 2: Direction     (0 = grows up)
 *   Bit 1: Readable/Writable
 *   Bit 0: Accessed      (CPU sets this; we init to 0)
 *
 * GRANULARITY byte:
 *   Bit 7: Granularity   (1 = limit in 4KB blocks)
 *   Bit 6: Size          (0 = 64-bit segment, NOT 32-bit)
 *   Bit 5: Long mode     (1 = 64-bit code segment)
 *   Bit 4: Reserved
 *   Bits 3-0: Limit high nibble
 */
static gdt_entry_t make_entry(uint8_t access, uint8_t granularity) {
    gdt_entry_t e;
    e.limit_low  = 0;
    e.base_low   = 0;
    e.base_mid   = 0;
    e.access     = access;
    e.granularity = granularity;
    e.base_high  = 0;
    return e;
}

void gdt_init(void) {
    /*
     * Entry 0: Null descriptor
     * Required — the CPU will fault if CS or SS points to index 0.
     * We intentionally leave it zeroed.
     */
    gdt[0] = make_entry(0x00, 0x00);

    /*
     * Entry 1: Kernel Code Segment
     *
     * Access = 0x9A:
     *   1001 1010
     *   │    │└── Accessed=0
     *   │    └─── Readable=1
     *   │         Conforming=0
     *   │         Executable=1 (code)
     *   │         Descriptor=1 (code/data type)
     *   └───────  DPL=0 (ring 0), Present=1
     *
     * Granularity = 0xA0:
     *   1010 0000
     *   │└── Long mode = 1 (64-bit)  ← CRITICAL for long mode
     *   └─── Granularity = 1
     *        Size = 0  (must be 0 when Long=1)
     */
    gdt[1] = make_entry(0x9A, 0xA0);

    /*
     * Entry 2: Kernel Data Segment
     *
     * Access = 0x92:
     *   1001 0010
     *   │    │└── Accessed=0
     *   │    └─── Writable=1
     *   │         Expand-down=0
     *   │         Executable=0 (data, not code)
     *   │         Descriptor=1
     *   └───────  DPL=0, Present=1
     *
     * Granularity = 0x00 — long mode data segments ignore these bits
     */
    gdt[2] = make_entry(0x92, 0x00);

    /* Set up the GDTR — pointer the CPU will read with lgdt */
    gdtr.limit = (uint16_t)(sizeof(gdt) - 1);
    gdtr.base  = (uint64_t)&gdt;

    /*
     * Load GDT and reload all segment registers.
     *
     * lgdt   — loads the new GDT pointer into the CPU
     *
     * We MUST reload CS via a far return (retfq) because
     * mov cannot write to CS directly. The trick is:
     *   push  new_cs_selector
     *   push  return_address (via lea)
     *   retfq  (pops both: sets RIP = addr, CS = selector)
     *
     * Then reload the data segment registers (DS, ES, SS, FS, GS)
     * with the kernel data selector (0x10).
     *
     * FS and GS are set to 0 — they'll be used for TLS later.
     */
    __asm__ volatile (
        "lgdt %0\n"

        /* Reload CS with a far return */
        "lea  1f(%%rip), %%rax\n"
        "push %1\n"                  /* push kernel code selector (0x08) */
        "push %%rax\n"               /* push return address */
        "lretq\n"                    /* far return: pops RIP then CS */
        "1:\n"

        /* Reload data segment registers */
        "mov  %2, %%ax\n"
        "mov  %%ax, %%ds\n"
        "mov  %%ax, %%es\n"
        "mov  %%ax, %%ss\n"
        "xor  %%ax, %%ax\n"         /* 0 = null selector for FS/GS (fine in 64-bit) */
        "mov  %%ax, %%fs\n"
        "mov  %%ax, %%gs\n"

        :
        : "m"(gdtr),
          "i"((uint64_t)GDT_KERNEL_CODE),
          "i"((uint16_t)GDT_KERNEL_DATA)
        : "rax", "memory"
    );
}