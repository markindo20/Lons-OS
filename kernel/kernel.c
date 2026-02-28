/*
 * kernel.c — Lons OS Entry Point
 */

#include "limine.h"
#include "framebuffer.h"
#include "pmm.h"
#include "gdt.h"
#include "idt.h"
#include "vmm.h"
#include "heap.h"
#include "pic.h"
#include "keyboard.h"

__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests")))
static volatile struct limine_framebuffer_request fb_req = {
    .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0
};
__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST, .revision = 0
};
__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST, .revision = 0
};
__attribute__((used, section(".requests")))
static volatile struct limine_kernel_address_request kaddr_req = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST, .revision = 0
};

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void cpu_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

#define kprint     fb_console_print
#define kprint_hex fb_console_print_hex
#define kprint_dec fb_console_print_dec

static void print_ok(void) {
    fb_console_set_color(0x0044FF88, 0x00000000);
    kprint("OK\n");
    fb_console_set_color(0x00FFFFFF, 0x00000000);
}
static void print_fail(const char *msg) {
    fb_console_set_color(0x00FF4444, 0x00000000);
    kprint(msg); kprint("\n");
    cpu_halt();
}

void _start(void) {

    /* ── 1. Framebuffer ── */
    if (!fb_req.response || fb_req.response->framebuffer_count == 0) cpu_halt();
    fb_init(fb_req.response);
    fb_console_init();

    for (uint32_t y = 0; y < 36; y++)
        for (uint32_t x = 0; x < g_fb.width; x++)
            fb_put_pixel(x, y, 0x001E1E2E);
    fb_print_at(8, 10, "Lons OS", 0x00FFFFFF, 0x001E1E2E);
    fb_print_at(g_fb.width - 120, 10, "Booting...", 0x00AAAAAA, 0x001E1E2E);

    fb_console_set_color(0x0044FF88, 0x00000000);
    kprint("\n\n  Lons OS Boot Log\n  ----------------\n\n");
    fb_console_set_color(0x00FFFFFF, 0x00000000);

    /* ── 2. GDT ── */
    kprint("  [GDT] Loading descriptor tables...    "); gdt_init(); print_ok();

    /* ── 3. PIC — remap BEFORE enabling IDT interrupts ── */
    kprint("  [PIC] Remapping interrupt controller... "); pic_init(); print_ok();

    /* ── 4. IDT ── */
    kprint("  [IDT] Installing exception handlers... "); idt_init(); print_ok();

    /* ── 5. Validate responses ── */
    if (!memmap_req.response) print_fail("  [FATAL] No memory map!");
    if (!hhdm_req.response)   print_fail("  [FATAL] No HHDM!");
    if (!kaddr_req.response)  print_fail("  [FATAL] No kernel address!");

    uint64_t hhdm_offset = hhdm_req.response->offset;
    uint64_t kern_phys   = kaddr_req.response->physical_base;
    uint64_t kern_virt   = kaddr_req.response->virtual_base;
    kprint("  [BOOT] HHDM: "); kprint_hex(hhdm_offset); kprint("\n");

    /* ── 6. PMM ── */
    kprint("  [PMM] Initializing...                  ");
    pmm_init(memmap_req.response, hhdm_offset); print_ok();
    kprint("  [PMM] ");
    kprint_dec((pmm_get_free_pages() * 4096) / (1024*1024));
    kprint(" MB free\n");

    /* ── 7. VMM ── */
    kprint("  [VMM] Building page tables...          ");
    vmm_init(hhdm_offset, memmap_req.response, kern_phys, kern_virt); print_ok();

    /* ── 8. Heap ── */
    kprint("  [HEAP] Initializing 16MB heap...       ");
    heap_init(kern_virt + (512ULL * 1024 * 1024), 16ULL * 1024 * 1024); print_ok();

    /* ── 9. Keyboard ── */
    kprint("  [KB] Enabling PS/2 keyboard...         ");
    keyboard_init();
    print_ok();

    /*
     * Enable hardware interrupts — sti (Set Interrupt Flag).
     * From this point the CPU will respond to IRQs.
     * IRQ1 (keyboard) will now fire on every keypress.
     *
     * We call this AFTER keyboard_init() so the IDT + PIC
     * are fully configured before the first interrupt arrives.
     */
    __asm__ volatile ("sti");
    kprint("  [CPU] Interrupts enabled\n");

    /* ── Boot complete ── */
    for (uint32_t y = 0; y < 36; y++)
        for (uint32_t x = 0; x < g_fb.width; x++)
            fb_put_pixel(x, y, 0x00182030);
    fb_print_at(8,  10, "Lons OS", 0x00FFFFFF, 0x00182030);
    fb_print_at(g_fb.width - 80, 10, "READY", 0x0044FF88, 0x00182030);

    fb_console_set_color(0x0044FF88, 0x00000000);
    kprint("\n  All systems ONLINE\n");
    fb_console_set_color(0x00AAAAAA, 0x00000000);
    kprint("  GDT | PIC | IDT | PMM | VMM | HEAP | KB\n\n");

    /* ── Interactive keyboard echo shell ── */
    fb_console_set_color(0x004488FF, 0x00000000);
    kprint("  Type anything — keystrokes appear below:\n");
    fb_console_set_color(0x00FFFFFF, 0x00000000);
    kprint("  > ");

    /*
     * Main loop — wait for keypresses and echo them to screen.
     * This is the foundation of a future shell / GUI event loop.
     *
     * keyboard_getchar() spins with hlt until a key is ready,
     * which means the CPU sleeps between keystrokes — efficient.
     */
    while (1) {
        char c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            kprint("\n  > ");
        } else if (c == '\b') {
            /* Basic backspace — just print indicator for now */
            kprint("\b \b");
        } else {
            /* Echo single character */
            char str[2] = { c, 0 };
            fb_console_print(str);
        }
    }
}