#pragma once
#include <stdint.h>
#include <stddef.h>

/*
 * heap.h — Kernel Heap Allocator
 *
 * Implements kmalloc() / kfree() / krealloc() on top of VMM + PMM.
 *
 * Strategy: Free-list allocator with block headers
 *
 * Memory layout of a heap block:
 *
 *   ┌────────────────────────────────┐  ← heap_block_t (header)
 *   │  size    (uint64_t)            │    size of DATA region only
 *   │  free    (uint8_t)  1=free     │
 *   │  magic   (uint32_t) 0xDEADC0DE│    corruption detection
 *   │  next    (pointer)             │    next block in list
 *   │  prev    (pointer)             │    previous block in list
 *   ├────────────────────────────────┤  ← what kmalloc() returns
 *   │  DATA ...                      │
 *   │  (size bytes)                  │
 *   └────────────────────────────────┘
 *
 * The heap starts with one giant free block covering all
 * initially committed virtual pages. On each kmalloc():
 *   1. Find first free block large enough (first-fit)
 *   2. Split it if the remainder is worth keeping
 *   3. Mark it used, return pointer to DATA
 *
 * On kfree():
 *   1. Walk back from DATA pointer to find header
 *   2. Mark block free
 *   3. Coalesce with adjacent free blocks (prevent fragmentation)
 */

/* Canary value stored in every block header.
 * If this is wrong when we access a block, memory was corrupted. */
#define HEAP_MAGIC 0xDEADC0DE

/* Minimum split size — don't split a block if the remainder
 * would be smaller than this (header + minimum data). */
#define HEAP_MIN_SPLIT 64

typedef struct heap_block {
    uint64_t         size;   /* Size of data region (NOT including header) */
    uint8_t          free;   /* 1 = free, 0 = allocated                    */
    uint32_t         magic;  /* HEAP_MAGIC — corruption check               */
    struct heap_block *next; /* Next block (higher address)                 */
    struct heap_block *prev; /* Previous block (lower address)              */
} __attribute__((packed)) heap_block_t;

/*
 * heap_init — Set up the kernel heap.
 *
 * Reserves a region of virtual address space for the heap,
 * maps initial pages, and creates the first free block.
 *
 * @start      : virtual address where the heap begins
 * @initial_size : bytes to commit immediately (must be page-aligned)
 */
void heap_init(uint64_t start, uint64_t initial_size);

/*
 * kmalloc — Allocate at least `size` bytes.
 * Returns a pointer to usable memory, or NULL if out of memory.
 * The returned memory is NOT zeroed.
 */
void *kmalloc(size_t size);

/*
 * kzalloc — Like kmalloc but zeroes the returned memory.
 */
void *kzalloc(size_t size);

/*
 * kfree — Release memory allocated by kmalloc/kzalloc/krealloc.
 * Passing NULL is safe (no-op).
 */
void kfree(void *ptr);

/*
 * krealloc — Resize an allocation.
 * If ptr is NULL, behaves like kmalloc(size).
 * If size is 0, behaves like kfree(ptr).
 */
void *krealloc(void *ptr, size_t size);

/* ── Diagnostics ── */
uint64_t heap_get_used(void);
uint64_t heap_get_free(void);
uint64_t heap_get_total(void);