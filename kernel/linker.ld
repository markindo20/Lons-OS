/* linker.ld
 * We tell the linker:
 *   - Output format: 64-bit ELF for x86_64
 *   - Load address:  0xFFFFFFFF80000000 (higher-half, standard convention)
 *     This puts the kernel in the upper half of virtual memory,
 *     away from user programs (which will live in lower half later).
 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

PHDRS {
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2)); /* R+X: code   */
    rodata  PT_LOAD FLAGS((1 << 2));             /* R:   read-only data */
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2)); /* R+W: data   */
}

SECTIONS {
    /* Start at the higher half kernel base */
    . = 0xFFFFFFFF80000000;

    /* ── Limine request sections ── must come first */
    .requests_start_marker : {
        KEEP(*(.requests_start_marker))
    } :rodata

    .requests : {
        KEEP(*(.requests))
    } :rodata

    .requests_end_marker : {
        KEEP(*(.requests_end_marker))
    } :rodata

    /* ── Executable code ── */
    .text : {
        *(.text .text.*)
    } :text

    /* ── Read-only data (constants, strings) ── */
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    /* ── Read-write data (globals, BSS) ── */
    .data : {
        *(.data .data.*)
    } :data

    /* BSS: zero-initialized globals
     * The bootloader zeroes this for us. */
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data
}