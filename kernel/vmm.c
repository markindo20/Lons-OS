/*
 * vmm.c — Virtual Memory Manager
 */

#include "vmm.h"
#include "pmm.h"

pagemap_t g_kernel_pagemap = {0};
static uint64_t g_hhdm = 0;

static inline uint64_t *phys_to_virt(uint64_t phys) {
    return (uint64_t *)(phys + g_hhdm);
}

static uint64_t alloc_table(void) {
    uint64_t phys = (uint64_t)pmm_alloc_page();
    if (!phys) return 0;
    uint64_t *virt = phys_to_virt(phys);
    for (int i = 0; i < 512; i++) virt[i] = 0;
    return phys;
}

static uint64_t get_or_create_table(uint64_t *entry_ptr, uint64_t flags) {
    if (*entry_ptr & VMM_FLAG_PRESENT)
        return ENTRY_PHYS(*entry_ptr);
    uint64_t new_phys = alloc_table();
    if (!new_phys) return 0;
    *entry_ptr = new_phys | flags;
    return new_phys;
}

void vmm_map_page(pagemap_t *pagemap, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t iflags = VMM_FLAG_PRESENT | VMM_FLAG_WRITABLE;
    if (flags & VMM_FLAG_USER) iflags |= VMM_FLAG_USER;

    uint64_t *pml4   = phys_to_virt(pagemap->pml4_phys);
    uint64_t pdpt_p  = get_or_create_table(&pml4[PML4_INDEX(virt)], iflags);
    if (!pdpt_p) return;

    uint64_t *pdpt   = phys_to_virt(pdpt_p);
    uint64_t pd_p    = get_or_create_table(&pdpt[PDPT_INDEX(virt)], iflags);
    if (!pd_p) return;

    uint64_t *pd     = phys_to_virt(pd_p);
    uint64_t pt_p    = get_or_create_table(&pd[PD_INDEX(virt)], iflags);
    if (!pt_p) return;

    uint64_t *pt     = phys_to_virt(pt_p);
    pt[PT_INDEX(virt)] = phys | flags;
}

void vmm_unmap_page(pagemap_t *pagemap, uint64_t virt) {
    uint64_t *pml4 = phys_to_virt(pagemap->pml4_phys);
    uint64_t pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & VMM_FLAG_PRESENT)) return;

    uint64_t *pdpt = phys_to_virt(ENTRY_PHYS(pml4e));
    uint64_t pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & VMM_FLAG_PRESENT)) return;

    uint64_t *pd   = phys_to_virt(ENTRY_PHYS(pdpte));
    uint64_t pde   = pd[PD_INDEX(virt)];
    if (!(pde & VMM_FLAG_PRESENT)) return;

    uint64_t *pt   = phys_to_virt(ENTRY_PHYS(pde));
    pt[PT_INDEX(virt)] = 0;
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

uint64_t vmm_virt_to_phys(pagemap_t *pagemap, uint64_t virt) {
    uint64_t *pml4 = phys_to_virt(pagemap->pml4_phys);
    uint64_t pml4e = pml4[PML4_INDEX(virt)];
    if (!(pml4e & VMM_FLAG_PRESENT)) return 0;

    uint64_t *pdpt = phys_to_virt(ENTRY_PHYS(pml4e));
    uint64_t pdpte = pdpt[PDPT_INDEX(virt)];
    if (!(pdpte & VMM_FLAG_PRESENT)) return 0;

    uint64_t *pd   = phys_to_virt(ENTRY_PHYS(pdpte));
    uint64_t pde   = pd[PD_INDEX(virt)];
    if (!(pde & VMM_FLAG_PRESENT)) return 0;

    uint64_t *pt   = phys_to_virt(ENTRY_PHYS(pde));
    uint64_t pte   = pt[PT_INDEX(virt)];
    if (!(pte & VMM_FLAG_PRESENT)) return 0;

    return ENTRY_PHYS(pte) + (virt & 0xFFF);
}

void vmm_load(pagemap_t *pagemap) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pagemap->pml4_phys) : "memory");
}

void vmm_init(uint64_t hhdm_offset,
              struct limine_memmap_response *memmap,
              uint64_t kernel_phys_base,
              uint64_t kernel_virt_base) {
    g_hhdm = hhdm_offset;

    g_kernel_pagemap.pml4_phys = alloc_table();
    if (!g_kernel_pagemap.pml4_phys) return;

    /*
     * Mapping 1: Full HHDM — every physical page accessible as
     *            virt (hhdm_offset + phys) → phys
     */
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        struct limine_memmap_entry *e = memmap->entries[i];
        uint64_t base = e->base & ~(uint64_t)0xFFF;
        uint64_t top  = (e->base + e->length + 0xFFF) & ~(uint64_t)0xFFF;
        for (uint64_t phys = base; phys < top; phys += 4096) {
            vmm_map_page(&g_kernel_pagemap,
                         phys + hhdm_offset,
                         phys,
                         VMM_KERNEL_DATA);
        }
    }

    /*
     * Mapping 2: Kernel code/data
     *
     * Limine tells us EXACTLY where it loaded the kernel:
     *   kernel_phys_base = actual physical load address
     *   kernel_virt_base = 0xFFFFFFFF80000000 (from our linker script)
     *
     * We map 64MB from the physical base so we cover the kernel,
     * its BSS, stack, and any early allocations.
     *
     * virt (kernel_virt_base + off) → phys (kernel_phys_base + off)
     */
    for (uint64_t off = 0; off < 64ULL * 1024 * 1024; off += 4096) {
        vmm_map_page(&g_kernel_pagemap,
                     kernel_virt_base + off,
                     kernel_phys_base + off,
                     VMM_KERNEL_DATA);
    }

    /* Switch CR3 — our page tables are now live */
    vmm_load(&g_kernel_pagemap);
}