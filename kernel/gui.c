/*
 * gui.c — Lons OS Window Manager
 *
 * Draws a macOS-inspired desktop directly to the framebuffer.
 * No GPU, no compositor — just pixel blitting.
 */

#include "gui.h"
#include "framebuffer.h"

/* ── Font dimensions (must match framebuffer.c) ── */
#define FONT_W 8
#define FONT_H 16

/* ── Global state ── */
gui_window_t g_windows[GUI_MAX_WINDOWS];
int          g_focused_window = -1;

/* ─────────────────────────────────────────────
 * Internal helpers
 * ───────────────────────────────────────────── */

/* Draw a filled circle — used for traffic light buttons */
static void draw_circle(int32_t cx, int32_t cy, int32_t r, uint32_t color) {
    for (int32_t dy = -r; dy <= r; dy++) {
        for (int32_t dx = -r; dx <= r; dx++) {
            if (dx*dx + dy*dy <= r*r) {
                fb_put_pixel((uint32_t)(cx + dx), (uint32_t)(cy + dy), color);
            }
        }
    }
}

/* Draw text centered horizontally within a bounding box */
static void draw_text_centered(int32_t box_x, int32_t box_y,
                                uint32_t box_w, uint32_t box_h,
                                const char *str, uint32_t fg, uint32_t bg) {
    /* Count string length */
    int len = 0;
    while (str[len]) len++;

    uint32_t text_w = (uint32_t)(len * FONT_W);
    uint32_t text_h = FONT_H;

    int32_t tx = box_x + (int32_t)(box_w - text_w) / 2;
    int32_t ty = box_y + (int32_t)(box_h - text_h) / 2;

    if (tx < box_x) tx = box_x;
    if (ty < box_y) ty = box_y;

    fb_print_at((uint32_t)tx, (uint32_t)ty, str, fg, bg);
}

/* Content area dimensions for a window */
static inline uint32_t content_x(int wid) {
    return (uint32_t)(g_windows[wid].x + WIN_BORDER);
}
static inline uint32_t content_y(int wid) {
    return (uint32_t)(g_windows[wid].y + TITLEBAR_H);
}
static inline uint32_t content_w(int wid) {
    return g_windows[wid].w - (uint32_t)(WIN_BORDER * 2);
}
static inline uint32_t content_h(int wid) {
    return g_windows[wid].h - (uint32_t)TITLEBAR_H - (uint32_t)WIN_BORDER;
}

/* ─────────────────────────────────────────────
 * gui_init — Draw the desktop
 * ───────────────────────────────────────────── */
void gui_init(void) {
    /* Zero out window slots */
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        g_windows[i].visible = 0;
        g_windows[i].focused = 0;
    }
    g_focused_window = -1;

    /* ── Desktop background ── */
    /* Gradient-like effect: slightly lighter at top, darker at bottom */
    for (uint32_t y = MENUBAR_H; y < g_fb.height; y++) {
        /* Interpolate from top color to bottom color */
        uint32_t t = (y - MENUBAR_H) * 255 / (g_fb.height - MENUBAR_H);

        /* Top: #1D3557, Bottom: #0A1628 */
        uint8_t r = (uint8_t)(0x1D - (0x1D - 0x0A) * t / 255);
        uint8_t g = (uint8_t)(0x35 - (0x35 - 0x16) * t / 255);
        uint8_t b = (uint8_t)(0x57 - (0x57 - 0x28) * t / 255);
        uint32_t color = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        fb_draw_hline(0, y, g_fb.width, color);
    }

    /* ── Menu bar ── */
    gui_draw_menubar();
}

/* ─────────────────────────────────────────────
 * gui_draw_menubar
 * ───────────────────────────────────────────── */
void gui_draw_menubar(void) {
    /* Background */
    fb_fill_rect(0, 0, g_fb.width, MENUBAR_H, GUI_MENUBAR);
    /* Subtle bottom shadow line */
    fb_draw_hline(0, MENUBAR_H - 1, g_fb.width, 0x00111111);

    /* Apple-style logo / OS name on the left */
    fb_print_at(12, (MENUBAR_H - FONT_H) / 2,
                "  Lons OS ", GUI_MENUFG, GUI_MENUBAR);

    /* Menu items */
    fb_print_at(100, (MENUBAR_H - FONT_H) / 2,
                "File    View    Help", GUI_MENUFG, GUI_MENUBAR);

    /* Clock placeholder on the right */
    const char *clock_str = "00:00";
    uint32_t clock_x = g_fb.width - (5 * FONT_W) - 16;
    fb_print_at(clock_x, (MENUBAR_H - FONT_H) / 2,
                clock_str, GUI_MENUFG, GUI_MENUBAR);
}

/* ─────────────────────────────────────────────
 * gui_window_create
 * ───────────────────────────────────────────── */
static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

int gui_window_create(int32_t x, int32_t y, uint32_t w, uint32_t h,
                      const char *title) {
    /* Find a free slot */
    int wid = -1;
    for (int i = 0; i < GUI_MAX_WINDOWS; i++) {
        if (!g_windows[i].visible) { wid = i; break; }
    }
    if (wid == -1) return -1;

    gui_window_t *win = &g_windows[wid];
    win->x       = x;
    win->y       = y;
    win->w       = w;
    win->h       = h;
    win->visible = 1;
    win->focused = 0;
    win->con_x   = content_x(wid);
    win->con_y   = content_y(wid);
    win->con_fg  = GUI_WINFG;
    win->con_bg  = GUI_WINBG;
    str_copy(win->title, title, 64);

    gui_window_draw(wid);
    return wid;
}

/* ─────────────────────────────────────────────
 * gui_window_draw — Redraw window frame
 * ───────────────────────────────────────────── */
void gui_window_draw(int wid) {
    gui_window_t *win = &g_windows[wid];
    if (!win->visible) return;

    int32_t  wx = win->x,   wy = win->y;
    uint32_t ww = win->w,   wh = win->h;

    /* ── Outer border ── */
    fb_draw_rect((uint32_t)wx, (uint32_t)wy, ww, wh, GUI_WINBORDER);

    /* ── Title bar ── */
    uint32_t tb_color = win->focused ? GUI_TITLEBAR_ACTIVE : GUI_TITLEBAR;
    fb_fill_rect((uint32_t)(wx + WIN_BORDER),
                 (uint32_t)(wy + WIN_BORDER),
                 ww - (uint32_t)(WIN_BORDER * 2),
                 (uint32_t)(TITLEBAR_H - WIN_BORDER),
                 tb_color);

    /* ── Traffic light buttons ── */
    int32_t btn_y  = wy + TITLEBAR_H / 2;
    int32_t btn_x1 = wx + TRAFFIC_PAD + TRAFFIC_R;
    int32_t btn_x2 = btn_x1 + TRAFFIC_GAP;
    int32_t btn_x3 = btn_x2 + TRAFFIC_GAP;

    draw_circle(btn_x1, btn_y, TRAFFIC_R, GUI_BTN_CLOSE);
    draw_circle(btn_x2, btn_y, TRAFFIC_R, GUI_BTN_MIN);
    draw_circle(btn_x3, btn_y, TRAFFIC_R, GUI_BTN_MAX);

    /* Subtle ring around each button */
    draw_circle(btn_x1, btn_y, TRAFFIC_R + 1, GUI_BTN_RING);
    draw_circle(btn_x2, btn_y, TRAFFIC_R + 1, GUI_BTN_RING);
    draw_circle(btn_x3, btn_y, TRAFFIC_R + 1, GUI_BTN_RING);
    /* Redraw buttons on top of rings */
    draw_circle(btn_x1, btn_y, TRAFFIC_R, GUI_BTN_CLOSE);
    draw_circle(btn_x2, btn_y, TRAFFIC_R, GUI_BTN_MIN);
    draw_circle(btn_x3, btn_y, TRAFFIC_R, GUI_BTN_MAX);

    /* ── Window title (centered in title bar) ── */
    draw_text_centered(wx + 60, wy + WIN_BORDER,
                       ww - 80,
                       (uint32_t)(TITLEBAR_H - WIN_BORDER),
                       win->title,
                       win->focused ? 0x00FFFFFF : 0x00AAAAAA,
                       tb_color);

    /* ── Content area background ── */
    fb_fill_rect(content_x(wid), content_y(wid),
                 content_w(wid), content_h(wid),
                 GUI_WINBG);
}

/* ─────────────────────────────────────────────
 * gui_window_focus
 * ───────────────────────────────────────────── */
void gui_window_focus(int wid) {
    /* Unfocus previous */
    if (g_focused_window >= 0 && g_focused_window < GUI_MAX_WINDOWS) {
        g_windows[g_focused_window].focused = 0;
        gui_window_draw(g_focused_window);  /* Redraw with inactive colors */
    }
    /* Focus new */
    g_focused_window = wid;
    if (wid >= 0) {
        g_windows[wid].focused = 1;
        gui_window_draw(wid);  /* Redraw with active colors */
    }
}

/* ─────────────────────────────────────────────
 * Per-window text output
 *
 * Each window maintains its own cursor (con_x, con_y).
 * Text wraps within the content area and scrolls up.
 * ───────────────────────────────────────────── */

static void win_scroll(int wid) {
    gui_window_t *win = &g_windows[wid];
    uint32_t cx = content_x(wid);
    uint32_t cy = content_y(wid);
    uint32_t cw = content_w(wid);
    uint32_t ch = content_h(wid);
    uint32_t bot = cy + ch;
    uint32_t rpx = g_fb.pitch / 4;

    /* Copy rows up by FONT_H pixels */
    for (uint32_t y = cy; y < bot - FONT_H; y++) {
        uint32_t *dst = g_fb.addr + y           * rpx + cx;
        uint32_t *src = g_fb.addr + (y + FONT_H) * rpx + cx;
        for (uint32_t x = 0; x < cw; x++) dst[x] = src[x];
    }
    /* Clear bottom line */
    for (uint32_t y = bot - FONT_H; y < bot; y++) {
        uint32_t *row = g_fb.addr + y * rpx + cx;
        for (uint32_t x = 0; x < cw; x++) row[x] = win->con_bg;
    }

    if (win->con_y >= cy + FONT_H) win->con_y -= FONT_H;
}

static void win_newline(int wid) {
    gui_window_t *win = &g_windows[wid];
    win->con_x  = content_x(wid);
    win->con_y += FONT_H;
    if (win->con_y + FONT_H > content_y(wid) + content_h(wid)) {
        win_scroll(wid);
    }
}

void gui_print(int wid, const char *str) {
    if (wid < 0 || wid >= GUI_MAX_WINDOWS) return;
    gui_window_t *win = &g_windows[wid];
    if (!win->visible) return;

    while (*str) {
        if (*str == '\n') {
            win_newline(wid);
        } else {
            fb_put_char(win->con_x, win->con_y, *str, win->con_fg, win->con_bg);
            win->con_x += FONT_W;
            if (win->con_x + FONT_W > content_x(wid) + content_w(wid)) {
                win_newline(wid);
            }
        }
        str++;
    }
}

void gui_print_hex(int wid, uint64_t val) {
    char buf[19]; const char *h = "0123456789ABCDEF";
    buf[0]='0'; buf[1]='x'; buf[18]=0;
    for (int i = 15; i >= 2; i--) { buf[i] = h[val & 0xF]; val >>= 4; }
    gui_print(wid, buf);
}

void gui_print_dec(int wid, uint64_t val) {
    char buf[21]; int i = 19; buf[20] = 0;
    if (!val) { gui_print(wid, "0"); return; }
    while (val && i > 0) { buf[--i] = (char)('0' + (val % 10)); val /= 10; }
    gui_print(wid, &buf[i]);
}

void gui_set_color(int wid, uint32_t fg) {
    if (wid >= 0 && wid < GUI_MAX_WINDOWS)
        g_windows[wid].con_fg = fg;
}

void gui_backspace(int wid) {
    if (wid < 0 || wid >= GUI_MAX_WINDOWS) return;
    gui_window_t *win = &g_windows[wid];
    if (!win->visible) return;

    uint32_t cx = content_x(wid);
    uint32_t cy = content_y(wid);

    /* Can't go before top-left of content */
    if (win->con_x <= cx && win->con_y <= cy) return;

    if (win->con_x > cx) {
        win->con_x -= FONT_W;
    } else {
        win->con_y -= FONT_H;
        win->con_x  = cx + ((content_w(wid) / FONT_W) - 1) * FONT_W;
    }
    fb_fill_rect(win->con_x, win->con_y, FONT_W, FONT_H, win->con_bg);
}

void gui_newline(int wid) {
    if (wid >= 0 && wid < GUI_MAX_WINDOWS)
        win_newline(wid);
}

void gui_clear_window(int wid) {
    if (wid < 0 || wid >= GUI_MAX_WINDOWS) return;
    gui_window_t *win = &g_windows[wid];
    fb_fill_rect(content_x(wid), content_y(wid),
                 content_w(wid), content_h(wid), win->con_bg);
    win->con_x = content_x(wid);
    win->con_y = content_y(wid);
}