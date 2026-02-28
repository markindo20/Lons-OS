/*
 * kernel.c — Lons OS Entry Point
 * Uses Limine framebuffer — no deprecated terminal API.
 */

#include "limine.h"
#include "framebuffer.h"
#include "pmm.h"
#include "gdt.h"
#include "idt.h"

/* ── Limine Requests ── */
__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

/* Framebuffer — replaces the old deprecated terminal */
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

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;

static void cpu_halt(void) {
    for (;;) __asm__ volatile ("cli; hlt");
}

/* kprint now goes through the framebuffer console */
#define kprint     fb_console_print
#define kprint_hex fb_console_print_hex
#define kprint_dec fb_console_print_dec

void _start(void) {

    /* ── 1. Framebuffer ── */
    if (!fb_req.response || fb_req.response->framebuffer_count == 0)
        cpu_halt(); /* No framebuffer — nothing we can do */

    fb_init(fb_req.response);
    fb_console_init();  /* Black screen, cursor at top-left */

    /* Draw a colored header bar */
    for (uint32_t y = 0; y < 36; y++)
        for (uint32_t x = 0; x < g_fb.width; x++)
            fb_put_pixel(x, y, 0x001E1E2E);  /* Dark navy */

    fb_print_at(8, 10, "Lons OS", FB_WHITE, 0x001E1E2E);
    fb_print_at(g_fb.width - 120, 10, "Booting...", FB_GRAY, 0x001E1E2E);

    /* Console starts below the header */
    fb_console_set_color(FB_GREEN, FB_BLACK);
    kprint("\n\n  Lons OS Boot Log\n");
    kprint("  ----------------\n\n");
    fb_console_set_color(FB_WHITE, FB_BLACK);

    /* ── 2. GDT ── */
    kprint("  [GDT] Loading descriptor tables... ");
    gdt_init();
    fb_console_set_color(FB_GREEN, FB_BLACK);
    kprint("OK\n");
    fb_console_set_color(FB_WHITE, FB_BLACK);

    /* ── 3. IDT ── */
    kprint("  [IDT] Installing exception handlers... ");
    idt_init();
    fb_console_set_color(FB_GREEN, FB_BLACK);
    kprint("OK\n");
    fb_console_set_color(FB_WHITE, FB_BLACK);

    /* ── 4. Memory map + HHDM ── */
    if (!memmap_req.response || memmap_req.response->entry_count == 0) {
        fb_console_set_color(FB_RED, FB_BLACK);
        kprint("  [FATAL] No memory map from Limine!\n");
        cpu_halt();
    }
    if (!hhdm_req.response) {
        fb_console_set_color(FB_RED, FB_BLACK);
        kprint("  [FATAL] No HHDM from Limine!\n");
        cpu_halt();
    }

    uint64_t hhdm_offset = hhdm_req.response->offset;
    kprint("  [BOOT] HHDM offset: ");
    kprint_hex(hhdm_offset);
    kprint("\n");

    /* ── 5. PMM ── */
    kprint("  [PMM] Initializing physical memory... ");
    pmm_init(memmap_req.response, hhdm_offset);
    fb_console_set_color(FB_GREEN, FB_BLACK);
    kprint("OK\n");
    fb_console_set_color(FB_WHITE, FB_BLACK);

    kprint("  [PMM] RAM: ");
    kprint_dec((pmm_get_free_pages() * 4096) / (1024 * 1024));
    kprint(" MB free / ");
    kprint_dec((pmm_get_total_pages() * 4096) / (1024 * 1024));
    kprint(" MB total\n");

    /* ── 6. PMM smoke test ── */
    void *p1 = pmm_alloc_page();
    void *p2 = pmm_alloc_page();
    if (!p1 || !p2) {
        fb_console_set_color(FB_RED, FB_BLACK);
        kprint("  [PMM] FAIL - out of memory!\n");
        cpu_halt();
    }
    uint64_t *v1 = (uint64_t *)((uint64_t)p1 + hhdm_offset);
    uint64_t *v2 = (uint64_t *)((uint64_t)p2 + hhdm_offset);
    *v1 = 0xDEADBEEFCAFEBABEULL;
    *v2 = 0x1234567890ABCDEFULL;
    if (*v1 != 0xDEADBEEFCAFEBABEULL || *v2 != 0x1234567890ABCDEFULL) {
        fb_console_set_color(FB_RED, FB_BLACK);
        kprint("  [PMM] FAIL - memory R/W mismatch!\n");
        cpu_halt();
    }
    pmm_free_page(p1);
    pmm_free_page(p2);
    fb_console_set_color(FB_GREEN, FB_BLACK);
    kprint("  [PMM] Smoke test passed\n");
    fb_console_set_color(FB_WHITE, FB_BLACK);

    /* ── 7. IDT live test ── */
    kprint("  [IDT] Testing exception handling (int3)... ");
    __asm__ volatile ("int3");
    /* If IDT is broken → triple fault → QEMU resets
       If IDT works    → handler runs, prints crash info, halts */

    /* ── All done ── */
    fb_console_set_color(FB_GREEN, FB_BLACK);
    kprint("\n  All systems ONLINE\n");
    fb_console_set_color(FB_WHITE, FB_BLACK);
    kprint("  Framebuffer | GDT | IDT | PMM\n");
    kprint("  CPU halting.\n");

    cpu_halt();
}