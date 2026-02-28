/*
 * kernel.c — Entry point for Lons OS
 *
 * Rules:
 *   - NO standard library. We are freestanding.
 *   - NO C runtime startup (no _start from libc).
 *   - Limine sets us up in 64-bit long mode already.
 */

#include "limine.h"
#include "pmm.h"

/* ─────────────────────────────────────────────
 * SECTION 1: Limine Request Structs
 * ───────────────────────────────────────────── */

/* Bootloader info — sanity check we booted via Limine */
__attribute__((used, section(".requests")))
static volatile struct limine_bootloader_info_request bootloader_info_req = {
    .id = LIMINE_BOOTLOADER_INFO_REQUEST,
    .revision = 0
};

/* Terminal — text output before we have our own driver */
__attribute__((used, section(".requests")))
static volatile struct limine_terminal_request terminal_req = {
    .id = LIMINE_TERMINAL_REQUEST,
    .revision = 0
};

/*
 * Memory Map Request — Limine tells us:
 *   - Where usable RAM is
 *   - Where the kernel/modules are loaded
 *   - What's reserved by firmware (ACPI, BIOS, etc.)
 *
 * This is the PRIMARY input to our PMM.
 */
__attribute__((used, section(".requests")))
static volatile struct limine_memmap_request memmap_req = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

/*
 * HHDM (Higher Half Direct Map) Request.
 *
 * Limine maps ALL physical RAM into a region of virtual memory
 * so we can read/write physical addresses without enabling
 * our own paging scheme yet.
 *
 * Example: physical 0x00001000 → virtual (hhdm_offset + 0x00001000)
 *
 * We MUST have this to safely place and access our bitmap.
 */
__attribute__((used, section(".requests")))
static volatile struct limine_hhdm_request hhdm_req = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

/* Required section boundary markers — do not remove */
__attribute__((used, section(".requests_start_marker")))
static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".requests_end_marker")))
static volatile LIMINE_REQUESTS_END_MARKER;


/* ─────────────────────────────────────────────
 * SECTION 2: Helpers
 * ───────────────────────────────────────────── */

static void cpu_halt(void) {
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/*
 * kprint — Write a string to Limine's terminal.
 * We need this before we have our own terminal driver.
 *
 * We store the terminal and write fn as statics so
 * every subsystem can call kprint after init.
 */
static struct limine_terminal *g_terminal = 0;
static limine_terminal_write   g_write    = 0;

static void kprint(const char *str) {
    if (!g_terminal || !g_write) return;

    /* Manual strlen — we have no stdlib */
    uint64_t len = 0;
    while (str[len]) len++;

    g_write(g_terminal, str, len);
}

/*
 * kprint_hex — Print a uint64_t value in hex.
 * Useful for printing addresses and sizes without printf.
 */
static void kprint_hex(uint64_t value) {
    char buf[19];   /* "0x" + 16 hex digits + null */
    const char *hex = "0123456789ABCDEF";

    buf[0]  = '0';
    buf[1]  = 'x';
    buf[18] = '\0';

    for (int i = 15; i >= 2; i--) {
        buf[i] = hex[value & 0xF];
        value >>= 4;
    }
    kprint(buf);
}

/*
 * kprint_dec — Print a uint64_t as decimal.
 * Used to show MB of RAM in a readable form.
 */
static void kprint_dec(uint64_t value) {
    char buf[21];
    int  i = 19;
    buf[20] = '\0';

    if (value == 0) {
        kprint("0");
        return;
    }

    buf[i] = '\0';
    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    kprint(&buf[i]);
}


/* ─────────────────────────────────────────────
 * SECTION 3: Kernel Entry Point
 * ───────────────────────────────────────────── */
void _start(void) {

    /* ── Step 1: Validate terminal ── */
    if (terminal_req.response == 0 ||
        terminal_req.response->terminal_count == 0) {
        cpu_halt();
    }

    g_terminal = terminal_req.response->terminals[0];
    g_write    = terminal_req.response->write;

    /* Clear screen, move cursor home */
    g_write(g_terminal, "\033[2J\033[H", 7);

    kprint("Lons OS — Booting...\n\n");

    /* ── Step 2: Validate memory map ── */
    if (memmap_req.response == 0 ||
        memmap_req.response->entry_count == 0) {
        kprint("[FATAL] No memory map from Limine!\n");
        cpu_halt();
    }

    /* ── Step 3: Validate HHDM ── */
    if (hhdm_req.response == 0) {
        kprint("[FATAL] No HHDM response from Limine!\n");
        cpu_halt();
    }

    uint64_t hhdm_offset = hhdm_req.response->offset;

    kprint("[BOOT] HHDM offset: ");
    kprint_hex(hhdm_offset);
    kprint("\n");

    /* ── Step 4: Print memory map ── */
    kprint("[BOOT] Memory map:\n");

    struct limine_memmap_response *memmap = memmap_req.response;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];

        /* Type name lookup — readable output */
        const char *type_name;
        switch (e->type) {
            case LIMINE_MEMMAP_USABLE:                 type_name = "Usable";          break;
            case LIMINE_MEMMAP_RESERVED:               type_name = "Reserved";        break;
            case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       type_name = "ACPI Reclaimable";break;
            case LIMINE_MEMMAP_ACPI_NVS:               type_name = "ACPI NVS";        break;
            case LIMINE_MEMMAP_BAD_MEMORY:             type_name = "Bad Memory";      break;
            case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: type_name = "Bootloader Recl.";break;
            case LIMINE_MEMMAP_KERNEL_AND_MODULES:     type_name = "Kernel/Modules";  break;
            case LIMINE_MEMMAP_FRAMEBUFFER:            type_name = "Framebuffer";     break;
            default:                                   type_name = "Unknown";         break;
        }

        kprint("  [");
        kprint_hex(e->base);
        kprint(" - ");
        kprint_hex(e->base + e->length);
        kprint("] ");
        kprint(type_name);
        kprint("\n");
    }

    /* ── Step 5: Initialize the Physical Memory Manager ── */
    kprint("\n[PMM] Initializing...\n");

    pmm_init(memmap, hhdm_offset);

    uint64_t free   = pmm_get_free_pages();
    uint64_t total  = pmm_get_total_pages();

    kprint("[PMM] Total pages tracked : ");
    kprint_dec(total);
    kprint("  (");
    kprint_dec((total * PAGE_SIZE) / (1024 * 1024));
    kprint(" MB)\n");

    kprint("[PMM] Free pages available: ");
    kprint_dec(free);
    kprint("  (");
    kprint_dec((free * PAGE_SIZE) / (1024 * 1024));
    kprint(" MB)\n");

    /* ── Step 6: Quick smoke test — alloc and free a page ── */
    kprint("\n[PMM] Smoke test...\n");

    void *page1 = pmm_alloc_page();
    void *page2 = pmm_alloc_page();

    if (page1 == 0 || page2 == 0) {
        kprint("[PMM] FAIL — could not allocate test pages!\n");
        cpu_halt();
    }

    kprint("[PMM] Allocated page1 @ phys ");
    kprint_hex((uint64_t)page1);
    kprint("\n");

    kprint("[PMM] Allocated page2 @ phys ");
    kprint_hex((uint64_t)page2);
    kprint("\n");

    /* Write to pages via HHDM to verify they're accessible */
    uint64_t *virt1 = (uint64_t *)((uint64_t)page1 + hhdm_offset);
    uint64_t *virt2 = (uint64_t *)((uint64_t)page2 + hhdm_offset);
    *virt1 = 0xDEADBEEFCAFEBABEULL;
    *virt2 = 0x1234567890ABCDEFULL;

    if (*virt1 != 0xDEADBEEFCAFEBABEULL || *virt2 != 0x1234567890ABCDEFULL) {
        kprint("[PMM] FAIL — memory read/write mismatch!\n");
        cpu_halt();
    }

    pmm_free_page(page1);
    pmm_free_page(page2);

    kprint("[PMM] Read/write OK. Free after test: ");
    kprint_dec(pmm_get_free_pages());
    kprint(" pages\n");

    kprint("\n[PMM] Physical Memory Manager — ONLINE\n");
    kprint("\nLons OS ready. CPU halting.\n");

    cpu_halt();
}