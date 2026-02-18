#ifndef SPINLOCK_H
#define SPINLOCK_H
#include "types.h"
#include "x86.h"

struct cpu {
  u8 apic_id;
  u8 ncli;           // depth of pushcli nesting
  u8 intena;         // were interrupts enabled before pushcli?
};

struct spinlock {
  u8 locked;
  char *name;
  struct cpu *cpu;
};

// per-cpu storage (simplified: single CPU for now)
static struct cpu boot_cpu;

static inline struct cpu* mycpu(void) {
  return &boot_cpu;
}

static inline void pushcli(void) {
  u64 rflags = read_rflags();
  cli();
  if (mycpu()->ncli == 0)
    mycpu()->intena = (rflags & 0x200) != 0;
  mycpu()->ncli++;
}

static inline void popcli(void) {
  if (read_rflags() & 0x200)
    hlt();  // panic: popcli with interrupts enabled
  if (mycpu()->ncli == 0)
    hlt();  // panic: popcli underflow
  mycpu()->ncli--;
  if (mycpu()->ncli == 0 && mycpu()->intena)
    sti();
}

static inline u8 holding(struct spinlock *lk) {
  u8 r;
  pushcli();
  r = lk->locked && lk->cpu == mycpu();
  popcli();
  return r;
}

static inline void acquire(struct spinlock *lk) {
  pushcli();
  if (holding(lk))
    hlt();  // panic: acquire held lock

  while (xchg(&lk->locked, 1) != 0)
    ;

  __sync_synchronize();
  lk->cpu = mycpu();
}

static inline void release(struct spinlock *lk) {
  if (!holding(lk))
    hlt();  // panic: release unheld lock

  lk->cpu = 0;
  __sync_synchronize();

  asm volatile("movb $0, %0" : "+m"(lk->locked) :);

  popcli();
}

static inline void initlock(struct spinlock *lk, char *name) {
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

#endif
