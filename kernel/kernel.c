/*
 * kernel.c — Lons OS Entry Point – with PIT Timer & RTC Clock
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
#include "mouse.h"
#include "gui.h"
#include "pit.h"
#include "rtc.h"

extern volatile uint64_t mouse_interrupt_count;

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
    fb_console_set_color(0x0044FF88, 0);
    kprint("OK\n");
    fb_console_set_color(0x00FFFFFF, 0);
}
static void print_fail(const char *msg) {
    fb_console_set_color(0x00FF4444, 0);
    kprint(msg); kprint("\n");
    fb_console_set_color(0x00FFFFFF, 0);
    cpu_halt();
}

/* ── Shell ── */
static int  term_wid  = -1;
static char input_line[256];
static int  input_len = 0;

static void shell_prompt(void) {
    gui_set_color(term_wid, 0x0044FF88);
    gui_print(term_wid, "\n  lons> ");
    gui_set_color(term_wid, GUI_WINFG);
}

static void shell_run(const char *line) {
    if (line[0]=='m'&&line[1]=='e'&&line[2]=='m'&&!line[3]) {
        gui_set_color(term_wid, 0x004488FF);
        gui_print(term_wid, "\n  Memory\n  ──────\n");
        gui_set_color(term_wid, GUI_WINFG);
        gui_print(term_wid, "  RAM:  "); gui_print_dec(term_wid,(pmm_get_free_pages()*4096)/(1024*1024)); gui_print(term_wid," MB free\n");
        gui_print(term_wid, "  Heap: "); gui_print_dec(term_wid,heap_get_free()/1024); gui_print(term_wid," KB free\n");
        return;
    }
    if (line[0]=='c'&&line[1]=='l'&&line[2]=='s'&&!line[3]) {
        gui_clear_window(term_wid);
        gui_set_color(term_wid, 0x0044FF88);
        gui_print(term_wid, "  Lons OS Terminal\n");
        gui_set_color(term_wid, GUI_WINFG);
        return;
    }
    if (line[0]=='h'&&line[1]=='e'&&line[2]=='l'&&line[3]=='p'&&!line[4]) {
        gui_set_color(term_wid, 0x004488FF);
        gui_print(term_wid, "\n  Commands\n  ────────\n");
        gui_set_color(term_wid, GUI_WINFG);
        gui_print(term_wid, "  mem     memory usage\n");
        gui_print(term_wid, "  cls     clear screen\n");
        gui_print(term_wid, "  time    show current time\n");
        gui_print(term_wid, "  date    show current date\n");
        gui_print(term_wid, "  uptime  seconds since boot\n");
        gui_print(term_wid, "  help    this list\n");
        return;
    }
    /* time command */
    if (line[0]=='t'&&line[1]=='i'&&line[2]=='m'&&line[3]=='e'&&!line[4]) {
        rtc_time_t now;
        rtc_read(&now);
        char buf[9];
        rtc_format_time(&now, buf);
        gui_set_color(term_wid, 0x004488FF);
        gui_print(term_wid, "\n  Time: ");
        gui_set_color(term_wid, GUI_WINFG);
        gui_print(term_wid, buf);
        gui_print(term_wid, "\n");
        return;
    }
    /* date command */
    if (line[0]=='d'&&line[1]=='a'&&line[2]=='t'&&line[3]=='e'&&!line[4]) {
        rtc_time_t now;
        rtc_read(&now);
        char dbuf[11];
        rtc_format_date(&now, dbuf);
        gui_set_color(term_wid, 0x004488FF);
        gui_print(term_wid, "\n  Date: ");
        gui_set_color(term_wid, GUI_WINFG);
        gui_print(term_wid, dbuf);
        gui_print(term_wid, "\n");
        return;
    }
    /* uptime command */
    if (line[0]=='u'&&line[1]=='p'&&line[2]=='t'&&line[3]=='i'
        &&line[4]=='m'&&line[5]=='e'&&!line[6]) {
        uint64_t secs = pit_get_seconds();
        uint64_t mins = secs / 60;
        uint64_t hrs  = mins / 60;
        gui_set_color(term_wid, 0x004488FF);
        gui_print(term_wid, "\n  Uptime: ");
        gui_set_color(term_wid, GUI_WINFG);
        gui_print_dec(term_wid, hrs); gui_print(term_wid, "h ");
        gui_print_dec(term_wid, mins % 60); gui_print(term_wid, "m ");
        gui_print_dec(term_wid, secs % 60); gui_print(term_wid, "s\n");
        return;
    }
    if (line[0]) {
        gui_set_color(term_wid, 0x00FF4444);
        gui_print(term_wid, "\n  Unknown: "); gui_print(term_wid, line); gui_print(term_wid, "\n");
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
        if (input_len > 0) { input_len--; gui_backspace(term_wid); }
    } else if (input_len < 254) {
        char s[2] = {c, 0};
        gui_print(term_wid, s);
        input_line[input_len++] = c;
    }
}

/* ── Mouse click handling ── */
static uint8_t last_mouse_buttons = 0;

static void handle_mouse_click(void) {
    uint8_t just_clicked = mouse_buttons & ~last_mouse_buttons;
    last_mouse_buttons = mouse_buttons;
    if (!(just_clicked & MOUSE_LEFT)) return;
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        gui_window_t *w = &g_windows[i];
        if (!w->visible) continue;
        int in_x  = (mouse_x >= w->x && mouse_x < w->x + (int32_t)w->w);
        int in_tb = (mouse_y >= w->y && mouse_y < w->y + TITLEBAR_H);
        if (in_x && in_tb) { gui_window_focus(i); return; }
    }
}

/* ── Menubar clock update ── */
static void update_menubar_clock(void) {
    rtc_time_t now;
    rtc_read(&now);
    char time_buf[9];
    rtc_format_time(&now, time_buf);

    // Position: right side of menubar (8 chars * 8px = 64px wide)
    uint32_t clock_x = g_fb.width - (8 * 8) - 16;
    uint32_t clock_y = (MENUBAR_H - 16) / 2;  // vertically centered

    // Clear old clock area and draw new one
    fb_fill_rect(clock_x, 0, 8 * 8 + 8, MENUBAR_H - 1, GUI_MENUBAR);
    fb_print_at(clock_x, clock_y, time_buf, GUI_MENUFG, GUI_MENUBAR);
}

/* ─────────────────────────────────────────────
 * _start
 * ───────────────────────────────────────────── */
void _start(void) {

    if (!fb_req.response || fb_req.response->framebuffer_count == 0) cpu_halt();
    fb_init(fb_req.response);
    fb_console_init();

    for (uint32_t y=0;y<36;y++) for (uint32_t x=0;x<g_fb.width;x++) fb_put_pixel(x,y,0x001E1E2E);
    fb_print_at(8,10,"Lons OS",0x00FFFFFF,0x001E1E2E);
    fb_print_at(g_fb.width-120,10,"Booting...",0x00AAAAAA,0x001E1E2E);

    fb_console_set_color(0x0044FF88,0);
    kprint("\n\n  Lons OS Boot Log\n  ----------------\n\n");
    fb_console_set_color(0x00FFFFFF,0);

    kprint("  [GDT]  Loading descriptor tables...    "); gdt_init();  print_ok();
    kprint("  [IDT]  Installing exception handlers... "); idt_init();  print_ok();

    if (!memmap_req.response||!memmap_req.response->entry_count) print_fail("  [FATAL] No memory map!");
    if (!hhdm_req.response)   print_fail("  [FATAL] No HHDM!");
    if (!kaddr_req.response)  print_fail("  [FATAL] No kernel address!");

    uint64_t hhdm   = hhdm_req.response->offset;
    uint64_t kphys  = kaddr_req.response->physical_base;
    uint64_t kvirt  = kaddr_req.response->virtual_base;

    kprint("  [PMM]  Initializing...                 "); pmm_init(memmap_req.response,hhdm); print_ok();
    kprint("  [VMM]  Building page tables...         "); vmm_init(hhdm,memmap_req.response,kphys,kvirt); print_ok();
    kprint("  [HEAP] Initializing...                 "); heap_init(kvirt+(512ULL*1024*1024),16ULL*1024*1024); print_ok();
    kprint("  [PIC]  Remapping controllers...        "); pic_init();  print_ok();
    kprint("  [PIT]  Starting timer (100 Hz)...      "); pit_init();  print_ok();
    kprint("  [KBD]  Installing keyboard driver...   "); kbd_init();  print_ok();

    // Unmask IRQ0 (PIT), IRQ1 (keyboard), IRQ12 (mouse)
    pic_unmask_irq(0);
    pic_unmask_irq(1);
    pic_unmask_irq(12);

    kprint("  [MOUSE] Installing mouse driver...     "); mouse_init(); print_ok();
    kprint("  [RTC]  Reading real-time clock...       ");
    {
        rtc_time_t boot_time;
        rtc_read(&boot_time);
        char tbuf[9], dbuf[11];
        rtc_format_time(&boot_time, tbuf);
        rtc_format_date(&boot_time, dbuf);
        kprint(dbuf); kprint(" "); kprint(tbuf); kprint(" ");
    }
    print_ok();

    kprint("  [CPU]  Enabling interrupts...          "); __asm__ volatile("sti"); print_ok();
    kprint("  [GUI]  Starting window manager...      ");

    for (volatile int i=0;i<50000000;i++);

    gui_init();

    uint32_t ww = (g_fb.width*3)/4;
    uint32_t wh = (g_fb.height*3)/4;
    int32_t  wx = (int32_t)((g_fb.width -ww)/2);
    int32_t  wy = (int32_t)((g_fb.height-wh)/2)+MENUBAR_H/2;

    term_wid = gui_window_create(wx, wy, ww, wh, "Terminal");
    gui_window_focus(term_wid);

    print_ok();

    gui_set_color(term_wid, 0x0044FF88);
    gui_print(term_wid, "  Welcome to Lons OS\n");
    gui_set_color(term_wid, 0x00AAAAAA);
    gui_print(term_wid, "  ──────────────────────────────────────\n");
    gui_set_color(term_wid, GUI_WINFG);
    gui_print(term_wid, "  Type 'help' for commands.\n");
    gui_print(term_wid, "  Mouse cursor active — click to focus.\n");
    gui_set_color(term_wid, 0x00AAAAAA);
    gui_print(term_wid, "  ──────────────────────────────────────\n");
    gui_set_color(term_wid, GUI_WINFG);

    shell_prompt();
    mouse_draw_cursor();

    /* Track last second to know when to update clock */
    uint64_t last_clock_tick = 0;

    /* ── Main event loop ── */
    while (1) {
        __asm__ volatile ("hlt");

        /* ── Update menubar clock once per second ── */
        uint64_t now_tick = pit_get_ticks();
        if (now_tick - last_clock_tick >= PIT_HZ) {
            last_clock_tick = now_tick;
            mouse_erase_cursor();
            update_menubar_clock();
            mouse_draw_cursor();
        }

        /* ── Mouse handling ── */
        if (mouse_poll()) {
            mouse_erase_cursor();
            handle_mouse_click();
            mouse_draw_cursor();
        }

        /* ── Keyboard handling ── */
        while (kbd_haschar()) {
            mouse_erase_cursor();
            shell_key(kbd_getchar());
            mouse_draw_cursor();
        }
    }
}