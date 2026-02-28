OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)

ENTRY(_start)

/* Define the Program Headers (PHDRS) with specific permissions */
PHDRS {
    text    PT_LOAD FLAGS((1 << 0) | (1 << 2)); /* Execute + Read */
    rodata  PT_LOAD FLAGS((1 << 2));             /* Read-Only */
    data    PT_LOAD FLAGS((1 << 1) | (1 << 2)); /* Write + Read */
}

SECTIONS {
    /* The kernel starts at the 2GB higher-half base address */
    . = 0xFFFFFFFF80000000;

    /* 1. Code Section (Executable) */
    .text : {
        *(.text .text.*)
    } :text

    /* 2. Read-Only Data Section (Aligned to a fresh 4KB page) */
    /* We put the Limine requests here so they don't fight with .text permissions */
    . = ALIGN(0x1000);

    .rodata : {
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
        *(.rodata .rodata.*)
    } :rodata

    /* 3. Read-Write Data Section (Aligned to a fresh 4KB page) */
    . = ALIGN(0x1000);

    .data : {
        *(.data .data.*)
    } :data

    /* 4. BSS Section (Uninitialized data, part of the Writable segment) */
    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    /* Discard sections that are not needed to keep the ELF clean */
    /DISCARD/ : {
        *(.eh_frame)
        *(.note .note.*)
    }
}