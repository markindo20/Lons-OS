/*
 * kernel.c — Entry point for MacOS-Lite
 *
 * Rules:
 *   - NO standard library. We are freestanding.
 *   - NO C runtime startup (no _start from libc).
 *   - Limine sets us up in 64-bit long mode already.
 */

#include "limine.h"

/* ─────────────────────────────────────────────
 * SECTION 1: Limine Request Structs
 *
 * These are magic global variables. Limine scans
 * your ELF binary for them by their unique IDs
 * and fills in the `.response` pointer before
 * your kernel runs. Think of them as a handshake.
 * ───────────────────────────────────────────── */

/*
 * Bootloader Info Request — optional, but useful
 * for sanity-checking we actually booted via Limine.
 */
__attribute__((used, section(".requests")))
static volatile struct limine_bootloader_info_request bootloader_info_req = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0
};

/*
 * Terminal Request — this is what gives us
 * a text-output function without needing VGA
 * or a UART driver yet.
 *
 * Limine sets up a terminal for us and hands us
 * a function pointer we can call to write strings.
 */
__attribute__((used, section(".requests")))
static volatile struct limine_terminal_request terminal_req = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0
};

/*
 * This special section marker tells Limine where
 * the requests section starts and ends in memory.
 * Required by the Limine spec — do not omit.
 */
__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;


/* ─────────────────────────────────────────────
 * SECTION 2: Helper — CPU Halt
 *
 * When we have nothing more to do, we:
 *   1. Disable interrupts (cli) — no external signals
 *   2. Halt the CPU (hlt)       — stops executing
 *
 * The loop is a safety net: on some systems hlt
 * can be woken by an NMI. We just halt again.
 * ───────────────────────────────────────────── */
static void cpu_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}


/* ─────────────────────────────────────────────
 * SECTION 3: Kernel Entry Point
 *
 * `_start` is what your linker script points to.
 * Limine will jump here after setup.
 * We declare it `void` — there is no one to
 * return to. We must never return from here.
 * ───────────────────────────────────────────── */
void _start(void) {

    /* ── Step 1: Validate the terminal response ──
     *
     * If Limine didn't give us a terminal (e.g., the
     * request was missing or the revision is wrong),
     * we cannot print anything. Just halt safely.
     */
    if (terminal_req.response == 0 ||
        terminal_req.response->terminal_count == 0) {
        cpu_halt();
    }

    /*
     * Grab the Limine-provided write function.
     * Its signature is:
     *   void write(struct limine_terminal*, const char*, uint64_t size)
     *
     * The terminal struct tells it WHICH terminal
     * to write to (there could be multiple).
     */
    struct limine_terminal *terminal = terminal_req.response->terminals[0];
    limine_terminal_write write = terminal_req.response->write;

    /* ── Step 2: Clear the screen ──
     *
     * Limine's terminal supports ANSI escape codes.
     * \033[2J  — clears the entire screen
     * \033[H   — moves cursor to top-left (Home)
     *
     * We pass the byte length manually (no strlen yet).
     */
    write(terminal, "\033[2J\033[H", 7);

    /* ── Step 3: Print our welcome message ── */
    write(terminal, "Welcome to MacOS-Lite\n", 22);

    /* ── Step 4: Halt — kernel's job here is done ── */
    cpu_halt();
}