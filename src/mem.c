#include "mem.h"
#include "spinlock.h"
#include "types.h"

u64 hhdm_offset;

struct run {
  struct run *next;
};

struct {
  struct spinlock lock;
  u8 use_lock;
  struct run *freelist;
} kmem;

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

void kfree(void *v) {
  struct run *r;

  if ((u64)v % PAGE_SIZE)
    return;

  memset(v, 1, PAGE_SIZE);

  if (kmem.use_lock)
    acquire(&kmem.lock);

  r = (struct run *)v;
  r->next = kmem.freelist;
  kmem.freelist = r;

  if (kmem.use_lock)
    release(&kmem.lock);
}

void *kalloc(void) {
  struct run *r;

  if (kmem.use_lock)
    acquire(&kmem.lock);

  r = kmem.freelist;
  if (r)
    kmem.freelist = r->next;

  if (kmem.use_lock)
    release(&kmem.lock);

  if (r)
    memset((void *)r, 5, PAGE_SIZE);

  return (void *)r;
}

void freerange(u64 phys_start, u64 phys_end) {
  // align start up to page boundary
  u64 p = (phys_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (; p + PAGE_SIZE <= phys_end; p += PAGE_SIZE) {
    kfree(PHYS_TO_VIRT(p));
  }
}

// Page table index extraction
#define PML4_INDEX(va) (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va) (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)   (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)   (((va) >> 12) & 0x1FF)

typedef u64 pte_t;

static inline pte_t* get_pml4(void) {
  u64 cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  return (pte_t*)PHYS_TO_VIRT(cr3 & ~0xFFFUL);
}

void map_page(u64 virt, u64 phys, u64 flags) {
  pte_t *pml4 = get_pml4();

  // Get or create PDPT
  if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc());
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pml4[PML4_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE;
  }
  pte_t *pdpt = PHYS_TO_VIRT(pml4[PML4_INDEX(virt)] & ~0xFFFUL);

  // Get or create PD
  if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc());
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pdpt[PDPT_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE;
  }
  pte_t *pd = PHYS_TO_VIRT(pdpt[PDPT_INDEX(virt)] & ~0xFFFUL);

  // Get or create PT
  if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc());
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pd[PD_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE;
  }
  pte_t *pt = PHYS_TO_VIRT(pd[PD_INDEX(virt)] & ~0xFFFUL);

  // Map the page
  pt[PT_INDEX(virt)] = (phys & ~0xFFFUL) | flags | PTE_PRESENT;

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
