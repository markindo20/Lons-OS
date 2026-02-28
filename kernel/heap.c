/*
 * heap.c — Kernel Heap Allocator
 *
 * Free-list allocator with coalescing.
 * Built on VMM (for virtual address space) + PMM (for physical pages).
 *
 * NO stdlib. Fully freestanding.
 */

#include "heap.h"
#include "vmm.h"
#include "pmm.h"

/* ── Internal state ── */
static heap_block_t *heap_head  = 0;   /* First block in the list  */
static uint64_t      heap_start = 0;   /* Virtual base of the heap */
static uint64_t      heap_size  = 0;   /* Total committed bytes     */

/* ─────────────────────────────────────────────
 * Internal helpers
 * ───────────────────────────────────────────── */

/* Our own memset — no stdlib */
static void heap_memset(void *ptr, uint8_t val, size_t n) {
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < n; i++) p[i] = val;
}

/* Our own memcpy — needed for krealloc */
static void heap_memcpy(void *dst, const void *src, size_t n) {
    uint8_t       *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) d[i] = s[i];
}

/*
 * get_header — Given a pointer returned by kmalloc(),
 * walk back sizeof(heap_block_t) bytes to find the header.
 */
static inline heap_block_t *get_header(void *ptr) {
    return (heap_block_t *)((uint8_t *)ptr - sizeof(heap_block_t));
}

/*
 * data_ptr — Given a block header, return pointer to its data region.
 */
static inline void *data_ptr(heap_block_t *block) {
    return (void *)((uint8_t *)block + sizeof(heap_block_t));
}

/*
 * validate_block — Check the magic number.
 * If it's wrong, someone wrote past a buffer boundary.
 * We halt instead of silently continuing with corrupted state.
 */
static void validate_block(heap_block_t *block) {
    if (block->magic != HEAP_MAGIC) {
        /* Heap corruption detected — halt immediately.
         * In a future version this will print a panic screen. */
        for (;;) __asm__ volatile ("cli; hlt");
    }
}

/*
 * split_block — If a free block is much larger than needed,
 * split it into two: one allocated block of `size` bytes,
 * and one smaller free block with the remainder.
 *
 * Before split:
 *   [  HEADER  |        DATA (big_size bytes)        ]
 *
 * After split:
 *   [  HEADER  | DATA (size bytes) ] [  HEADER  | DATA (remainder) ]
 *
 * We only split if the remainder is >= HEAP_MIN_SPLIT,
 * otherwise the leftover space is too small to be useful
 * and we just waste a little memory instead of fragmenting.
 */
static void split_block(heap_block_t *block, size_t size) {
    /* Would the remainder be large enough to be worth splitting? */
    size_t remainder = block->size - size;
    if (remainder <= sizeof(heap_block_t) + HEAP_MIN_SPLIT) {
        return; /* Not worth splitting — use the whole block */
    }

    /* Create a new header for the remainder block */
    heap_block_t *new_block = (heap_block_t *)((uint8_t *)data_ptr(block) + size);
    new_block->size  = remainder - sizeof(heap_block_t);
    new_block->free  = 1;
    new_block->magic = HEAP_MAGIC;
    new_block->next  = block->next;
    new_block->prev  = block;

    if (block->next) block->next->prev = new_block;

    block->size = size;
    block->next = new_block;
}

/*
 * coalesce — After freeing a block, merge it with any adjacent
 * free blocks to reduce fragmentation.
 *
 * We check:
 *   1. Can we merge with the NEXT block?
 *   2. Can we merge with the PREV block?
 *
 * Merging next:
 *   [THIS free][NEXT free] → [THIS (size + header + next->size) free]
 *
 * Merging prev:
 *   [PREV free][THIS free] → [PREV (size + header + this->size) free]
 */
static void coalesce(heap_block_t *block) {
    /* Merge with next block if it's free */
    if (block->next && block->next->free) {
        heap_block_t *next = block->next;
        validate_block(next);

        /* Absorb next: add its header + data into our size */
        block->size += sizeof(heap_block_t) + next->size;
        block->next  = next->next;
        if (next->next) next->next->prev = block;

        /* Wipe the now-absorbed header to prevent confusion */
        heap_memset(next, 0, sizeof(heap_block_t));
    }

    /* Merge with previous block if it's free */
    if (block->prev && block->prev->free) {
        heap_block_t *prev = block->prev;
        validate_block(prev);

        prev->size += sizeof(heap_block_t) + block->size;
        prev->next  = block->next;
        if (block->next) block->next->prev = prev;

        heap_memset(block, 0, sizeof(heap_block_t));
    }
}


/* ─────────────────────────────────────────────
 * heap_init
 * ───────────────────────────────────────────── */
void heap_init(uint64_t start, uint64_t initial_size) {
    heap_start = start;
    heap_size  = initial_size;

    /*
     * Map physical pages into the heap's virtual address range.
     * We use VMM + PMM to back the virtual addresses with real RAM.
     */
    for (uint64_t off = 0; off < initial_size; off += 4096) {
        void *phys = pmm_alloc_page();
        if (!phys) return; /* Out of memory during init */
        vmm_map_page(&g_kernel_pagemap,
                     start + off,
                     (uint64_t)phys,
                     VMM_KERNEL_DATA);
    }

    /*
     * Create the initial free block spanning the entire heap.
     * This is one giant free block that kmalloc() will carve from.
     */
    heap_head        = (heap_block_t *)start;
    heap_head->size  = initial_size - sizeof(heap_block_t);
    heap_head->free  = 1;
    heap_head->magic = HEAP_MAGIC;
    heap_head->next  = 0;
    heap_head->prev  = 0;
}


/* ─────────────────────────────────────────────
 * kmalloc
 * ───────────────────────────────────────────── */
void *kmalloc(size_t size) {
    if (size == 0) return 0;

    /* Align size to 16 bytes — ensures natural alignment for all
     * basic types (uint64_t, pointers, etc.) on x86_64. */
    size = (size + 15) & ~(size_t)15;

    /* First-fit search: walk the block list for a free block */
    heap_block_t *current = heap_head;
    while (current) {
        validate_block(current);

        if (current->free && current->size >= size) {
            /* Found a fit — split if much larger than needed */
            split_block(current, size);
            current->free = 0;
            return data_ptr(current);
        }
        current = current->next;
    }

    /* No free block large enough — heap is exhausted.
     * Future improvement: expand the heap by mapping more pages. */
    return 0;
}


/* ─────────────────────────────────────────────
 * kzalloc
 * ───────────────────────────────────────────── */
void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) heap_memset(ptr, 0, size);
    return ptr;
}


/* ─────────────────────────────────────────────
 * kfree
 * ───────────────────────────────────────────── */
void kfree(void *ptr) {
    if (!ptr) return; /* kfree(NULL) is always safe */

    heap_block_t *block = get_header(ptr);
    validate_block(block);

    if (block->free) {
        /* Double-free detected — halt.
         * Silently ignoring double-frees hides bugs. */
        for (;;) __asm__ volatile ("cli; hlt");
    }

    block->free = 1;

    /* Merge adjacent free blocks to fight fragmentation */
    coalesce(block);
}


/* ─────────────────────────────────────────────
 * krealloc
 * ───────────────────────────────────────────── */
void *krealloc(void *ptr, size_t size) {
    /* krealloc(NULL, size) == kmalloc(size) */
    if (!ptr) return kmalloc(size);

    /* krealloc(ptr, 0) == kfree(ptr) */
    if (size == 0) { kfree(ptr); return 0; }

    heap_block_t *block = get_header(ptr);
    validate_block(block);

    /* If the current block is already large enough, reuse it */
    if (block->size >= size) return ptr;

    /* Otherwise allocate new, copy, free old */
    void *new_ptr = kmalloc(size);
    if (!new_ptr) return 0;

    heap_memcpy(new_ptr, ptr, block->size); /* copy old data */
    kfree(ptr);
    return new_ptr;
}


/* ─────────────────────────────────────────────
 * Diagnostics
 * ───────────────────────────────────────────── */
uint64_t heap_get_used(void) {
    uint64_t used = 0;
    heap_block_t *b = heap_head;
    while (b) { if (!b->free) used += b->size; b = b->next; }
    return used;
}

uint64_t heap_get_free(void) {
    uint64_t free = 0;
    heap_block_t *b = heap_head;
    while (b) { if (b->free) free += b->size; b = b->next; }
    return free;
}

uint64_t heap_get_total(void) {
    return heap_size;
}