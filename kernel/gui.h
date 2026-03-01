#pragma once
#include <stdint.h>

/*
 * gui.h — Lons OS Window Manager
 *
 * macOS-inspired desktop with:
 *   - Solid desktop background
 *   - Top menu bar
 *   - Windows with title bars + traffic-light buttons
 *   - Per-window text console (each window is a terminal)
 *   - Keyboard input routed to focused window
 */

/* ── Layout constants ── */
#define MENUBAR_H     24    /* Menu bar height at top of screen     */
#define TITLEBAR_H    28    /* Window title bar height               */
#define WIN_BORDER    2     /* Window border thickness               */
#define TRAFFIC_R     7     /* Traffic light button radius (pixels)  */
#define TRAFFIC_PAD   10    /* Padding from left edge of title bar   */
#define TRAFFIC_GAP   18    /* Gap between traffic light centers     */

/* ── Colors ── */
#define GUI_DESKTOP   0x001D3557   /* Dark blue desktop                */
#define GUI_MENUBAR   0x002A2A2A   /* Dark grey menu bar               */
#define GUI_MENUFG    0x00EEEEEE   /* Menu bar text                    */
#define GUI_TITLEBAR  0x003C3C3C   /* Inactive title bar               */
#define GUI_TITLEBAR_ACTIVE 0x004A4A4A  /* Active window title bar     */
#define GUI_WINBG     0x001A1A2E   /* Window background (dark navy)    */
#define GUI_WINBORDER 0x00555555   /* Window border                    */
#define GUI_WINFG     0x00DDDDDD   /* Window text                      */
#define GUI_BTN_CLOSE 0x00FF5F57   /* Red close button                 */
#define GUI_BTN_MIN   0x00FEBC2E   /* Yellow minimize button           */
#define GUI_BTN_MAX   0x0028C840   /* Green maximize button            */
#define GUI_BTN_RING  0x00222222   /* Button outline ring              */

/* ── Maximum windows ── */
#define GUI_MAX_WINDOWS 8

/* ── Window structure ── */
typedef struct {
    /* Position and size */
    int32_t  x, y;           /* Top-left corner of the window frame  */
    uint32_t w, h;           /* Total width/height including title    */

    /* State */
    int      visible;        /* 1 = shown on screen                  */
    int      focused;        /* 1 = receives keyboard input          */

    /* Title */
    char     title[64];

    /* Content area console state (inside the window, below titlebar) */
    uint32_t con_x, con_y;   /* Current text cursor position         */
    uint32_t con_fg;          /* Text foreground color                */
    uint32_t con_bg;          /* Text background color (= GUI_WINBG)  */
} gui_window_t;

/* ─────────────────────────────────────────────
 * Core API
 * ───────────────────────────────────────────── */

/* gui_init — Draw desktop + menu bar. Call once at startup. */
void gui_init(void);

/*
 * gui_window_create — Create a new window.
 * Returns window index (0..GUI_MAX_WINDOWS-1), or -1 if no slots free.
 */
int gui_window_create(int32_t x, int32_t y, uint32_t w, uint32_t h,
                      const char *title);

/* gui_window_draw — Redraw a window's frame (border + titlebar). */
void gui_window_draw(int wid);

/* gui_window_focus — Give keyboard focus to window wid. */
void gui_window_focus(int wid);

/* gui_draw_menubar — Redraw the menu bar (call after window changes). */
void gui_draw_menubar(void);

/* Redraw a rectangular area of the screen (for cursor erasing) */
void gui_redraw_area(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* ─────────────────────────────────────────────
 * Per-window text output
 * ───────────────────────────────────────────── */

/* gui_print      — Print string to focused window's console */
void gui_print(int wid, const char *str);
void gui_print_hex(int wid, uint64_t val);
void gui_print_dec(int wid, uint64_t val);
void gui_set_color(int wid, uint32_t fg);
void gui_backspace(int wid);    /* Erase last character in window     */
void gui_newline(int wid);      /* Force newline in window            */
void gui_clear_window(int wid); /* Clear window content area          */

/* Global array of windows */
extern gui_window_t g_windows[GUI_MAX_WINDOWS];
extern int          g_focused_window;