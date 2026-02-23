#include "mem.h"
#include "spinlock.h"
#include "types.h"

u64 hhdm_offset;

// ---------------------------------------------------------------------------
// Binary buddy allocator
// order N manages blocks of 2^N pages (2^N * PAGE_SIZE bytes).
// MAX_ORDER-1 is the largest order (2^10 = 1024 pages = 4 MB).
// ---------------------------------------------------------------------------
#define MAX_ORDER 11

struct run {
  struct run *next;
};

struct buddy_state {
  struct spinlock lock;
  u8 use_lock;
  struct run *free_lists[MAX_ORDER];
};

static struct buddy_state buddy;

// Return smallest order o such that 2^o >= n (n > 0)
static u64 order_for(u64 n) {
  u64 o = 0;
  u64 size = 1;
  while (size < n) { size <<= 1; o++; }
  return o;
}

void *memset(void *dst, int c, u64 n) {
  u8 *d = (u8 *)dst;
  for (u64 i = 0; i < n; i++) {
    d[i] = (u8)c;
  }
  return dst;
}

void kinit(u64 hhdm) {
  hhdm_offset = hhdm;
  initlock(&buddy.lock, "buddy");
  buddy.use_lock = 0;
  for (int i = 0; i < MAX_ORDER; i++)
    buddy.free_lists[i] = 0;
}

// Remove block at address `addr` from free_lists[order]. Returns 1 if found.
static int buddy_remove(u64 addr, u64 order) {
  struct run **pp = &buddy.free_lists[order];
  while (*pp) {
    if ((u64)*pp == addr) {
      *pp = (*pp)->next;
      return 1;
    }
    pp = &(*pp)->next;
  }
  return 0;
}

void kfree(void *v, u64 npages) {
  if ((u64)v % PAGE_SIZE || npages == 0)
    return;

  u64 order = order_for(npages);
  if (order >= MAX_ORDER)
    order = MAX_ORDER - 1;

  u64 block_pages = (u64)1 << order;

  memset(v, MEM_FREE_PATTERN, block_pages * PAGE_SIZE);

  if (buddy.use_lock)
    acquire(&buddy.lock);

  u64 addr = (u64)v;

  // Walk up merging with buddy
  while (order < MAX_ORDER - 1) {
    u64 buddy_addr = addr ^ (PAGE_SIZE << order);
    if (!buddy_remove(buddy_addr, order))
      break;
    // Merged: the new block starts at the lower address
    if (buddy_addr < addr)
      addr = buddy_addr;
    order++;
  }

  struct run *r = (struct run *)addr;
  r->next = buddy.free_lists[order];
  buddy.free_lists[order] = r;

  if (buddy.use_lock)
    release(&buddy.lock);
}

void *kalloc(u64 npages) {
  if (npages == 0)
    return 0;

  u64 order = order_for(npages);
  if (order >= MAX_ORDER)
    return 0;

  if (buddy.use_lock)
    acquire(&buddy.lock);

  // Find smallest available order >= requested
  u64 k = order;
  while (k < MAX_ORDER && !buddy.free_lists[k])
    k++;

  if (k == MAX_ORDER) {
    if (buddy.use_lock)
      release(&buddy.lock);
    return 0;
  }

  // Pop block at order k
  struct run *block = buddy.free_lists[k];
  buddy.free_lists[k] = block->next;

  // Split down to requested order, returning upper halves to free lists
  while (k > order) {
    k--;
    u64 upper = (u64)block + (PAGE_SIZE << k);
    struct run *r = (struct run *)upper;
    r->next = buddy.free_lists[k];
    buddy.free_lists[k] = r;
  }

  if (buddy.use_lock)
    release(&buddy.lock);

  memset(block, MEM_ALLOC_PATTERN, npages * PAGE_SIZE);
  return (void *)block;
}

void freerange(u64 phys_start, u64 phys_end) {
  // align start up to page boundary
  u64 p = (phys_start + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

  for (; p + PAGE_SIZE <= phys_end; p += PAGE_SIZE) {
    kfree(PHYS_TO_VIRT(p), 1);
  }
}

void *memcpy(void *dst, const void *src, u64 n) {
  u8 *d = (u8 *)dst;
  const u8 *s = (const u8 *)src;
  for (u64 i = 0; i < n; i++)
    d[i] = s[i];
  return dst;
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

void map_page_pml4(u64 *pml4, u64 virt, u64 phys, u64 flags) {
  // Get or create PDPT
  if (!(pml4[PML4_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc(1));
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pml4[PML4_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE | PTE_USER;
  }
  pte_t *pdpt = PHYS_TO_VIRT(pml4[PML4_INDEX(virt)] & PAGE_FRAME_MASK);

  // Get or create PD
  if (!(pdpt[PDPT_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc(1));
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pdpt[PDPT_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE | PTE_USER;
  }
  pte_t *pd = PHYS_TO_VIRT(pdpt[PDPT_INDEX(virt)] & PAGE_FRAME_MASK);

  // Get or create PT
  if (!(pd[PD_INDEX(virt)] & PTE_PRESENT)) {
    u64 new_table = VIRT_TO_PHYS((u64)kalloc(1));
    memset(PHYS_TO_VIRT(new_table), 0, PAGE_SIZE);
    pd[PD_INDEX(virt)] = new_table | PTE_PRESENT | PTE_WRITE | PTE_USER;
  }
  pte_t *pt = PHYS_TO_VIRT(pd[PD_INDEX(virt)] & PAGE_FRAME_MASK);

  // Map the page
  pt[PT_INDEX(virt)] = (phys & PAGE_FRAME_MASK) | flags | PTE_PRESENT;
}

u64 *create_user_pml4(void) {
  u64 *new_pml4 = (u64 *)kalloc(1);
  if (!new_pml4) return 0;
  memset(new_pml4, 0, PAGE_SIZE);

  // Copy kernel-space PML4 entries (upper half: 256-511)
  pte_t *kernel_pml4 = get_pml4();
  for (int i = 256; i < 512; i++) {
    new_pml4[i] = kernel_pml4[i];
  }
  return new_pml4;
}
