#pragma once
#include <stdint.h>
#include "limine.h"

#define VMM_FLAG_PRESENT   (1ULL << 0)
#define VMM_FLAG_WRITABLE  (1ULL << 1)
#define VMM_FLAG_USER      (1ULL << 2)
/* NOTE: NX (bit 63) requires EFER.NXE — not enabling it yet.
 * We will add it properly when we implement proper section
 * permissions. For now all mappings are PRESENT | WRITABLE. */

/* Kernel mappings — no NX until EFER.NXE is set */
#define VMM_KERNEL_CODE  (VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE)
#define VMM_KERNEL_DATA  (VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE)
#define VMM_USER_CODE    (VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)
#define VMM_USER_DATA    (VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE | VMM_FLAG_USER)

#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)
#define ENTRY_PHYS(e)    ((e) & 0x000FFFFFFFFFF000ULL)

typedef struct {
    uint64_t pml4_phys;
} pagemap_t;

extern pagemap_t g_kernel_pagemap;

void vmm_init(uint64_t hhdm_offset,
              struct limine_memmap_response *memmap,
              uint64_t kernel_phys_base,
              uint64_t kernel_virt_base);

void vmm_map_page(pagemap_t *pagemap, uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap_page(pagemap_t *pagemap, uint64_t virt);
uint64_t vmm_virt_to_phys(pagemap_t *pagemap, uint64_t virt);
void vmm_load(pagemap_t *pagemap);