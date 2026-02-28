/*
 * pmm.h — Physical Memory Manager
 *
 * Bitmap-based page frame allocator.
 * Each bit tracks one 4KB physical page: 0=free, 1=used.
 *
 * We depend on Limine giving us two things:
 *   1. A memory map  — what physical RAM exists and its type
 *   2. An HHDM offset — so we can reach physical addrs as virtual ptrs
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "limine.h"

/* ── Page size constant ──
 * x86_64 standard page = 4096 bytes = 4KB
 * All allocations are one page at a time for now.
 */
#define PAGE_SIZE 4096ULL

/*
 * pmm_init — Parse Limine's memory map, place the bitmap,
 *            and mark all regions correctly.
 *
 * Must be called ONCE before any alloc/free.
 *
 * @memmap       : pointer to Limine's memmap response
 * @hhdm_offset  : virtual base where all physical RAM is mapped
 */
void pmm_init(struct limine_memmap_response *memmap, uint64_t hhdm_offset);

/*
 * pmm_alloc_page — Find the first free page, mark it used,
 *                  return its PHYSICAL address.
 *
 * Returns NULL if out of memory.
 * The caller is responsible for converting to a virtual address
 * using hhdm_offset if they need to write to it.
 */
void *pmm_alloc_page(void);

/*
 * pmm_free_page — Mark a previously-allocated physical page as free.
 *
 * @ptr : the PHYSICAL address returned by pmm_alloc_page()
 */
void pmm_free_page(void *ptr);

/* ── Diagnostic helpers ── */
uint64_t pmm_get_total_pages(void);
uint64_t pmm_get_free_pages(void);