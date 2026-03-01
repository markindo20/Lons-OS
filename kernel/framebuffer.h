#pragma once
#include <stdint.h>

/* ── Colors ── */
#define FB_BLACK     0x00000000
#define FB_WHITE     0x00FFFFFF
#define FB_RED       0x00FF4444
#define FB_GREEN     0x0044FF88
#define FB_BLUE      0x004488FF
#define FB_YELLOW    0x00FFFF44
#define FB_GRAY      0x00AAAAAA
#define FB_DARKGRAY  0x00333333
#define FB_NAVY      0x001E1E2E
#define FB_DARKNAVY  0x00182030

/* ── macOS-style palette ── */
#define FB_TITLEBAR  0x002D2D2D
#define FB_WINBG     0x001E1E1E
#define FB_DESKTOP   0x001A3A5C  /* macOS-like blue-grey desktop */
#define FB_ACCENT    0x000070C0
#define FB_CLOSE     0x00FF5F56
#define FB_MINIMIZE  0x00FFBD2E
#define FB_MAXIMIZE  0x0027C93F

typedef struct {
    uint32_t *addr;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint8_t   bpp;
} framebuffer_t;

extern framebuffer_t g_fb;

#include "limine.h"
int  fb_init(struct limine_framebuffer_response *response);

/* ── Pixel ── */
void fb_clear(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);

/* ── Shapes ── */
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_hline(uint32_t x, uint32_t y, uint32_t len, uint32_t color);
void fb_draw_vline(uint32_t x, uint32_t y, uint32_t len, uint32_t color);

/* ── Text ── */
void fb_put_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_print_at(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);

/* ── Scrolling console ── */
void fb_console_init(void);
void fb_console_init_region(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void fb_console_print(const char *str);
void fb_console_print_hex(uint64_t value);
void fb_console_print_dec(uint64_t value);
void fb_console_set_color(uint32_t fg, uint32_t bg);
void fb_console_backspace(void);   /* ← NEW: erase last character */