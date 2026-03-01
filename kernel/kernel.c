/*
 * kernel.c — Lons OS Entry Point
 * Boots into GUI desktop with a terminal window.
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
#include "gui.h"

/* ── Limine Requests ── */
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

/* ── Boot log uses raw framebuffer console ── */
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
    fb_console_set_color(0x00FFFFFF, 0x00000000);
    cpu_halt();
}

/* ─────────────────────────────────────────────
 * Shell — runs inside a GUI window
 * ───────────────────────────────────────────── */
static int   term_wid   = -1;  /* Terminal window ID     */
static char  input_line[256];
static int   input_len  = 0;

static void shell_prompt(void) {
    gui_set_color(term_wid, 0x0044FF88);
    gui_print(term_wid, "\n  lons> ");
    gui_set_color(term_wid, GUI_WINFG);
}

static void shell_run(const char *line) {
    /* mem */
    if (line[0]=='m' && line[1]=='e' && line[2]=='m' && !line[3]) {
        gui_set_color(term_wid, 0x004488FF);
        gui_print(term_wid, "\n  Memory Status\n  ─────────────\n");
        gui_set_color(term_wid, GUI_WINFG);
        gui_print(term_wid, "  RAM free:  ");
        gui_print_dec(term_wid, (pmm_get_free_pages() * 4096) / (1024*1024));
        gui_print(term_wid, " MB\n  Heap free: ");
        gui_print_dec(term_wid, heap_get_free() / 1024);
        gui_print(term_wid, " KB\n  Heap used: ");
        gui_print_dec(term_wid, heap_get_used());
        gui_print(term_wid, " bytes\n");
        return;
    }

    /* cls */
    if (line[0]=='c' && line[1]=='l' && line[2]=='s' && !line[3]) {
        gui_clear_window(term_wid);
        gui_set_color(term_wid, 0x0044FF88);
        gui_print(term_wid, "  Lons OS Terminal\n");
        gui_set_color(term_wid, GUI_WINFG);
        return;
    }

    /* help */
    if (line[0]=='h' && line[1]=='e' && line[2]=='l' && line[3]=='p' && !line[4]) {
        gui_set_color(term_wid, 0x004488FF);
        gui_print(term_wid, "\n  Commands\n  ────────\n");
        gui_set_color(term_wid, GUI_WINFG);
        gui_print(term_wid, "  mem    show memory usage\n");
        gui_print(term_wid, "  cls    clear terminal\n");
        gui_print(term_wid, "  help   this list\n");
        return;
    }

    /* unknown */
    if (line[0] != 0) {
        gui_set_color(term_wid, 0x00FF4444);
        gui_print(term_wid, "\n  Unknown: ");
        gui_print(term_wid, line);
        gui_print(term_wid, "\n");
        gui_set_color(term_wid, GUI_WINFG);
    }
}

static void shell_key(char c) {
    if (c == '\n') {
        input_line[input_len] = 0;
        gui_newline(term_wid);
        shell_run(input_line);
        input_len = 0;
        shell_prompt();

    } else if (c == '\b') {
        /* Backspace — delete last character */
        if (input_len > 0) {
            input_len--;
            gui_backspace(term_wid);  /* Erase pixel from screen */
        }

    } else if (input_len < 254) {
        char str[2] = {c, 0};
        gui_print(term_wid, str);
        input_line[input_len++] = c;
    }
}

/* ─────────────────────────────────────────────
 * _start — Kernel entry point
 * ───────────────────────────────────────────── */
void _start(void) {

    /* ── 1. Framebuffer (early — needed for boot log) ── */
    if (!fb_req.response || fb_req.response->framebuffer_count == 0)
        cpu_halt();
    fb_init(fb_req.response);
    fb_console_init();

    /* Boot log header */
    for (uint32_t y = 0; y < 36; y++)
        for (uint32_t x = 0; x < g_fb.width; x++)
            fb_put_pixel(x, y, 0x001E1E2E);
    fb_print_at(8, 10, "Lons OS", 0x00FFFFFF, 0x001E1E2E);
    fb_print_at(g_fb.width - 120, 10, "Booting...", 0x00AAAAAA, 0x001E1E2E);

    fb_console_set_color(0x0044FF88, 0x00000000);
    kprint("\n\n  Lons OS Boot Log\n");
    kprint("  ----------------\n\n");
    fb_console_set_color(0x00FFFFFF, 0x00000000);

    /* ── 2. GDT ── */
    kprint("  [GDT] Loading descriptor tables...    ");
    gdt_init(); print_ok();

    /* ── 3. IDT ── */
    kprint("  [IDT] Installing exception handlers... ");
    idt_init(); print_ok();

    /* ── 4. Validate Limine responses ── */
    if (!memmap_req.response || memmap_req.response->entry_count == 0)
        print_fail("  [FATAL] No memory map!");
    if (!hhdm_req.response)
        print_fail("  [FATAL] No HHDM!");
    if (!kaddr_req.response)
        print_fail("  [FATAL] No kernel address!");

    uint64_t hhdm_offset = hhdm_req.response->offset;
    uint64_t kern_phys   = kaddr_req.response->physical_base;
    uint64_t kern_virt   = kaddr_req.response->virtual_base;

    /* ── 5. PMM ── */
    kprint("  [PMM] Initializing...                 ");
    pmm_init(memmap_req.response, hhdm_offset); print_ok();
    kprint("  [PMM] RAM: ");
    kprint_dec((pmm_get_free_pages() * 4096) / (1024*1024));
    kprint(" MB free\n");

    /* ── 6. VMM ── */
    kprint("  [VMM] Building kernel page tables...  ");
    vmm_init(hhdm_offset, memmap_req.response, kern_phys, kern_virt);
    print_ok();

    /* ── 7. Heap ── */
    kprint("  [HEAP] Initializing...                ");
    heap_init(kern_virt + (512ULL * 1024 * 1024), 16ULL * 1024 * 1024);
    print_ok();

    /* ── 8. PIC ── */
    kprint("  [PIC] Remapping interrupt controller... ");
    pic_init(); print_ok();

    /* ── 9. Keyboard ── */
    kprint("  [KBD] Installing PS/2 driver...       ");
    kbd_init(); print_ok();

    /* ── 10. Enable interrupts ── */
    kprint("  [CPU] Enabling interrupts...          ");
    __asm__ volatile ("sti");
    print_ok();

    /* ── 11. GUI ── */
    kprint("  [GUI] Starting window manager...      ");

    /* Small delay loop — let boot log be visible briefly */
    for (volatile int i = 0; i < 50000000; i++);

    /* Draw desktop */
    gui_init();

    /* Create terminal window — centered on screen */
    uint32_t win_w = (g_fb.width  * 3) / 4;  /* 75% of screen width  */
    uint32_t win_h = (g_fb.height * 3) / 4;  /* 75% of screen height */
    int32_t  win_x = (int32_t)((g_fb.width  - win_w) / 2);
    int32_t  win_y = (int32_t)((g_fb.height - win_h) / 2) + MENUBAR_H / 2;

    term_wid = gui_window_create(win_x, win_y, win_w, win_h, "Terminal");
    gui_window_focus(term_wid);

    print_ok();  /* This goes to the old boot log console - harmless */

    /* ── Welcome message in the terminal window ── */
    gui_set_color(term_wid, 0x0044FF88);
    gui_print(term_wid, "  Welcome to Lons OS\n");
    gui_set_color(term_wid, 0x00AAAAAA);
    gui_print(term_wid, "  ──────────────────────────────────────\n");
    gui_set_color(term_wid, GUI_WINFG);
    gui_print(term_wid, "  Type 'help' to see available commands.\n");
    gui_print(term_wid, "  Backspace works. Shift works. Caps Lock works.\n");
    gui_set_color(term_wid, 0x00AAAAAA);
    gui_print(term_wid, "  ──────────────────────────────────────\n");
    gui_set_color(term_wid, GUI_WINFG);

    shell_prompt();

    /* ── Main event loop ── */
    while (1) {
        __asm__ volatile ("hlt"); /* Sleep until interrupt */

        while (kbd_haschar()) {
            char c = kbd_getchar();
            shell_key(c);
        }
    }
}