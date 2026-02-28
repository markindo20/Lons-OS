#pragma once
#include <stdint.h>

/*
 * framebuffer.h — Pixel framebuffer + text renderer
 *
 * Limine maps the GPU framebuffer into our virtual address space.
 * We can write pixels directly to it — no GPU driver needed.
 *
 * We embed a small 8x16 bitmap font so we can render text
 * without any file system or font loading.
 */

/* RGB color constants */
#define FB_BLACK    0x00000000
#define FB_WHITE    0x00FFFFFF
#define FB_RED      0x00FF4444
#define FB_GREEN    0x0044FF88
#define FB_BLUE     0x004488FF
#define FB_YELLOW   0x00FFFF44
#define FB_GRAY     0x00AAAAAA
#define FB_DARKGRAY 0x00333333

/* Framebuffer state — filled by fb_init */
typedef struct {
    uint32_t *addr;         /* Virtual address of pixel buffer    */
    uint32_t  width;        /* Screen width in pixels             */
    uint32_t  height;       /* Screen height in pixels            */
    uint32_t  pitch;        /* Bytes per row (may include padding)*/
    uint8_t   bpp;          /* Bits per pixel (we expect 32)      */
} framebuffer_t;

extern framebuffer_t g_fb;

/*
 * fb_init — Initialize framebuffer from Limine response.
 * Must be called before any drawing.
 * Returns 1 on success, 0 if no framebuffer available.
 */
#include "limine.h"
int  fb_init(struct limine_framebuffer_response *response);

/* ── Pixel drawing ── */
void fb_clear(uint32_t color);
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);

/* ── Text rendering ── */
void fb_put_char(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);
void fb_print_at(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);

/* ── Terminal-style scrolling text console ── */
void fb_console_init(void);
void fb_console_print(const char *str);
void fb_console_print_hex(uint64_t value);
void fb_console_print_dec(uint64_t value);
void fb_console_set_color(uint32_t fg, uint32_t bg);