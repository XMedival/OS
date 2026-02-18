#ifndef MEM_H
#define MEM_H
#include "types.h"

#define PAGE_SIZE 4096

extern u64 hhdm_offset;

#define PHYS_TO_VIRT(addr) ((void *)((u64)(addr) + hhdm_offset))
#define VIRT_TO_PHYS(addr) ((u64)(addr) - hhdm_offset)

// Page table flags
#define PTE_PRESENT  (1UL << 0)
#define PTE_WRITE    (1UL << 1)
#define PTE_USER     (1UL << 2)
#define PTE_PWT      (1UL << 3)  // Write-through
#define PTE_PCD      (1UL << 4)  // Cache disable
#define PTE_NX       (1UL << 63) // No execute

void freerange(u64 phys_start, u64 phys_end);
void kfree(void *v);
void *kalloc(void);
void *memset(void *dst, int c, u64 n);
void kinit(u64 hhdm);
void map_page(u64 virt, u64 phys, u64 flags);
void map_mmio(u64 phys, u64 size);

#endif
