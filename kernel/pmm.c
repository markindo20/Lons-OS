/*
 * pmm.c — Physical Memory Manager Implementation
 *
 * Strategy: Bitmap allocator
 *   - One bit per 4KB page frame across ALL physical RAM
 *   - Bitmap stored in the first usable region big enough to hold it
 *   - We access physical memory via Limine's HHDM mapping
 *
 * NO stdlib. NO malloc. We are fully freestanding.
 */

#include "pmm.h"

/* ─────────────────────────────────────────────
 * Internal state — only visible inside pmm.c
 * ───────────────────────────────────────────── */

/*
 * The bitmap itself.
 * This pointer will point INTO physical RAM (via HHDM).
 * We find a usable region big enough to hold it during init.
 */
static uint8_t *bitmap = 0;

/* Total number of 4KB page frames we're tracking */
static uint64_t total_pages = 0;

/* How many bytes the bitmap occupies (total_pages / 8, rounded up) */
static uint64_t bitmap_size = 0;

/* The HHDM virtual offset Limine gave us */
static uint64_t hhdm_base = 0;

/* Running count of free pages (updated on alloc/free) */
static uint64_t free_pages = 0;


/* ─────────────────────────────────────────────
 * Internal bitmap helpers
 *
 * We store 8 pages per byte. Bit 0 of byte 0 = page 0.
 * ───────────────────────────────────────────── */

/* Mark page N as USED (set bit) */
static inline void bitmap_set(uint64_t page_index) {
    bitmap[page_index / 8] |= (uint8_t)(1 << (page_index % 8));
}

/* Mark page N as FREE (clear bit) */
static inline void bitmap_clear(uint64_t page_index) {
    bitmap[page_index / 8] &= (uint8_t)~(1 << (page_index % 8));
}

/* Return 1 if page N is USED, 0 if FREE */
static inline int bitmap_test(uint64_t page_index) {
    return (bitmap[page_index / 8] >> (page_index % 8)) & 1;
}


/* ─────────────────────────────────────────────
 * Internal helper: fill memory (replaces memset)
 * We cannot use the stdlib memset — write our own.
 * ───────────────────────────────────────────── */
static void pmm_memset(void *ptr, uint8_t value, uint64_t size) {
    uint8_t *p = (uint8_t *)ptr;
    for (uint64_t i = 0; i < size; i++) {
        p[i] = value;
    }
}


/* ─────────────────────────────────────────────
 * Internal helper: align address UP to page boundary
 * e.g. 0x1001 → 0x2000
 * ───────────────────────────────────────────── */
static inline uint64_t align_up(uint64_t addr, uint64_t align) {
    return (addr + align - 1) & ~(align - 1);
}


/* ─────────────────────────────────────────────
 * pmm_init — The main initialization function
 * ───────────────────────────────────────────── */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset) {
    hhdm_base = hhdm_offset;

    /* ── Pass 1: Find the highest physical address ──
     *
     * We need to know how large our bitmap must be.
     * Scan every entry; track the highest base+length.
     */
    uint64_t highest_addr = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];
        uint64_t entry_end = entry->base + entry->length;

        if (entry_end > highest_addr) {
            highest_addr = entry_end;
        }
    }

    /*
     * Total pages = highest_addr / PAGE_SIZE
     * Bitmap bytes = total_pages / 8 (1 bit per page), rounded up
     */
    total_pages  = highest_addr / PAGE_SIZE;
    bitmap_size  = (total_pages + 7) / 8;  /* +7 ensures we round up */

    /* ── Pass 2: Find a usable region to store the bitmap ──
     *
     * The bitmap itself needs a home in physical RAM.
     * We scan for the first USABLE entry that's large enough.
     * We'll embed the bitmap at its base address.
     */
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        /* Only use freely available RAM */
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        /* Does this region fit our bitmap? */
        if (entry->length >= bitmap_size) {
            /*
             * Found it. Place the bitmap here.
             * We access this physical address via the HHDM:
             *   virtual = physical + hhdm_offset
             */
            bitmap = (uint8_t *)(entry->base + hhdm_base);

            /*
             * Start with ALL pages marked as USED (0xFF = all bits set).
             * We'll explicitly FREE only the actually usable pages below.
             * This is the safe default — unknown = reserved.
             */
            pmm_memset(bitmap, 0xFF, bitmap_size);

            break;
        }
    }

    /*
     * If bitmap is still null here, we have a serious problem —
     * no usable region was large enough. The caller should check
     * and halt. For now we just return (kernel.c will validate).
     */
    if (bitmap == 0) {
        return;
    }

    /* ── Pass 3: Mark all USABLE pages as FREE ──
     *
     * Walk the memory map again. For each USABLE region,
     * clear the corresponding bits in the bitmap.
     */
    free_pages = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *entry = memmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        /*
         * Align the region to page boundaries.
         * base: align UP (don't touch partial first page)
         * end:  align DOWN (don't touch partial last page)
         */
        uint64_t base = align_up(entry->base, PAGE_SIZE);
        uint64_t end  = (entry->base + entry->length) & ~(PAGE_SIZE - 1);

        for (uint64_t addr = base; addr < end; addr += PAGE_SIZE) {
            uint64_t page = addr / PAGE_SIZE;
            bitmap_clear(page);
            free_pages++;
        }
    }

    /* ── Pass 4: Re-mark the bitmap's own pages as USED ──
     *
     * The bitmap occupies real physical pages. If we don't
     * mark these as used, we might hand them out as "free"
     * and then overwrite our own tracking data. Fatal bug.
     *
     * Calculate how many pages the bitmap itself consumes.
     */
    uint64_t bitmap_phys_base  = (uint64_t)bitmap - hhdm_base;
    uint64_t bitmap_pages_used = (bitmap_size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (uint64_t p = 0; p < bitmap_pages_used; p++) {
        uint64_t page = (bitmap_phys_base / PAGE_SIZE) + p;
        if (!bitmap_test(page)) {
            /* Was counted as free — now it's not */
            bitmap_set(page);
            if (free_pages > 0) free_pages--;
        }
    }
}


/* ─────────────────────────────────────────────
 * pmm_alloc_page — Allocate one free 4KB page
 *
 * Linear scan through the bitmap for the first 0 bit.
 * Marks it 1 (used) and returns the physical address.
 *
 * Returns: physical address of the page, or NULL if OOM.
 * ───────────────────────────────────────────── */
void *pmm_alloc_page(void) {
    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            return (void *)(i * PAGE_SIZE);
        }
    }
    /* Out of memory */
    return 0;
}


/* ─────────────────────────────────────────────
 * pmm_free_page — Return a page to the free pool
 *
 * @ptr: the PHYSICAL address (as returned by pmm_alloc_page)
 * ───────────────────────────────────────────── */
void pmm_free_page(void *ptr) {
    uint64_t page = (uint64_t)ptr / PAGE_SIZE;
    bitmap_clear(page);
    free_pages++;
}


/* ─────────────────────────────────────────────
 * Diagnostic accessors
 * ───────────────────────────────────────────── */
uint64_t pmm_get_total_pages(void) { return total_pages; }
uint64_t pmm_get_free_pages(void)  { return free_pages;  }