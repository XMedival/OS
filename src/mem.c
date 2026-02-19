#include "mem.h"
#include "spinlock.h"
#include "types.h"

u64 hhdm_offset;

struct run {
  struct run *next;
};

struct kmem_state {
  struct spinlock lock;
  u8 use_lock;
  struct run *freelist;
};

static struct kmem_state kmem;

void *memset(void *dst, int c, u64 n) {
  u8 *d = (u8 *)dst;
  for (u64 i = 0; i < n; i++) {
    d[i] = (u8)c;
  }
  return dst;
}

void kinit(u64 hhdm) {
  hhdm_offset = hhdm;
  initlock(&kmem.lock, "kmem");
  kmem.use_lock = 0;
  kmem.freelist = 0;
}

void kfree(void *v, u64 npages) {
  if ((u64)v % PAGE_SIZE || npages == 0)
    return;

  if (kmem.use_lock)
    acquire(&kmem.lock);

  for (u64 i = 0; i < npages; i++) {
    void *page = (void *)((u64)v + i * PAGE_SIZE);
    memset(page, MEM_FREE_PATTERN, PAGE_SIZE);
    struct run *r = (struct run *)page;
    r->next = kmem.freelist;
    kmem.freelist = r;
  }

  if (kmem.use_lock)
    release(&kmem.lock);
}

// Find and remove n contiguous pages from freelist
// Returns pointer to first page, or NULL if not found
void *kalloc(u64 npages) {
  if (npages == 0)
    return 0;

  if (kmem.use_lock)
    acquire(&kmem.lock);

  void *result = 0;

  if (npages == 1) {
    // Fast path for single page
    struct run *r = kmem.freelist;
    if (r) {
      kmem.freelist = r->next;
      result = (void *)r;
    }
  } else {
    // Multi-page: find n contiguous pages
    // Walk freelist looking for a run of n consecutive pages
    struct run *r = kmem.freelist;

    while (r) {
      // Check if next n-1 pages are also free and contiguous
      u64 base = (u64)r;
      u64 found = 1;

      for (u64 i = 1; i < npages; i++) {
        u64 expected = base + i * PAGE_SIZE;
        // Search entire freelist for this page
        struct run *search = kmem.freelist;
        int found_page = 0;

        while (search) {
          if ((u64)search == expected) {
            found_page = 1;
            found++;
            break;
          }
          search = search->next;
        }

        if (!found_page)
          break;
      }

      if (found == npages) {
        // Found contiguous block - remove all pages from freelist
        result = (void *)base;

        // Remove each page from the freelist
        for (u64 i = 0; i < npages; i++) {
          u64 addr = base + i * PAGE_SIZE;
          struct run **p = &kmem.freelist;
          while (*p) {
            if ((u64)*p == addr) {
              *p = (*p)->next;
              break;
            }
            p = &(*p)->next;
          }
        }
        break;
      }

      r = r->next;
    }
  }

  if (kmem.use_lock)
    release(&kmem.lock);

  if (result)
    memset(result, MEM_ALLOC_PATTERN, npages * PAGE_SIZE);

  return result;
}

void freerange(u64 phys_start, u64 phys_end) {
  // align start up to page boundary
  u64 p = (phys_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (; p + PAGE_SIZE <= phys_end; p += PAGE_SIZE) {
    kfree(PHYS_TO_VIRT(p), 1);
  }
}

// Page table index extraction
#define PML4_INDEX(va) (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)   (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)   (((va) >> 12) & 0x1FF)

static inline pte_t* get_pml4(void) {
  u64 cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  return (pte_t*)PHYS_TO_VIRT(cr3 & PAGE_FRAME_MASK);
}

void map_page(u64 virt, u64 phys, u64 flags) {
  pte_t *pml4 = get_pml4();

  // Get or create PDPT
  if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc(1));
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pml4[PML4_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE;
  }
  pte_t *pdpt = PHYS_TO_VIRT(pml4[PML4_INDEX(virt)] & PAGE_FRAME_MASK);

  // Get or create PD
  if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc(1));
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pdpt[PDPT_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE;
  }
  pte_t *pd = PHYS_TO_VIRT(pdpt[PDPT_INDEX(virt)] & PAGE_FRAME_MASK);

  // Get or create PT
  if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc(1));
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pd[PD_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE;
  }
  pte_t *pt = PHYS_TO_VIRT(pd[PD_INDEX(virt)] & PAGE_FRAME_MASK);

  // Map the page
  pt[PT_INDEX(virt)] = (phys & PAGE_FRAME_MASK) | flags | PTE_PRESENT;

  // Invalidate TLB for this page
  asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void map_mmio(u64 phys, u64 size) {
  u64 start = phys & ~(PAGE_SIZE - 1);
  u64 end = (phys + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (u64 p = start; p < end; p += PAGE_SIZE) {
    u64 virt = (u64)PHYS_TO_VIRT(p);
    // MMIO: present, writable, cache-disable, write-through
    map_page(virt, p, PTE_PRESENT | PTE_WRITE | PTE_PCD | PTE_PWT);
  }
}
