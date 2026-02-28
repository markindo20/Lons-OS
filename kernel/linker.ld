OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

PHDRS {
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2)); /* R+X */
    rodata  PT_LOAD FLAGS((1 << 2));             /* R   */
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2)); /* R+W */
}

SECTIONS {
    . = 0xFFFFFFFF80000000;

    /* ── Limine requests — must be in rodata segment ── */
    .requests_start_marker : ALIGN(4096) {
        KEEP(*(.requests_start_marker))
    } :rodata

    .requests : {
        KEEP(*(.requests))
    } :rodata

    .requests_end_marker : {
        KEEP(*(.requests_end_marker))
    } :rodata

    /* ── Code — MUST start on a fresh page ── */
    .text : ALIGN(4096) {
        *(.text .text.*)
    } :text

    /* ── Read-only data — fresh page ── */
    .rodata : ALIGN(4096) {
        *(.rodata .rodata.*)
    } :rodata

    /* ── Read-write data — fresh page ── */
    .data : ALIGN(4096) {
        *(.data .data.*)
    } :data

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss .bss.*)
    } :data
}
