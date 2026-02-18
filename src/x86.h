#ifndef X86_H
#define X86_H
#include "types.h"

#define ISR_STUB(num)				\
  __attribute((naked)) void isr##num(void) {	\
    asm volatile(				\
    "push $0\n"					\
    "push $" #num "\n"				\
    "jmp isr_common\n"				\
    );						\
  }

#define ISR_STUB_ERR(num)			\
  __attribute((naked)) void isr##num(void) {	\
    asm volatile(				\
    "push $" #num "\n"				\
    "jmp isr_common\n"				\
    );						\
  }

static inline void outb(u16 port, u8 data) {
  asm volatile("outb %b0, %w1" : : "a"(data), "Nd"(port) : "memory");
}

static inline void outw(u16 port, u16 data) {
  asm volatile("outw %w0, %w1" : : "a"(data), "Nd"(port) : "memory");
}

static inline void outl(u16 port, u32 data) {
  asm volatile("outl %0, %w1" : : "a"(data), "Nd"(port) : "memory");
}

static inline u8 inb(u16 port) {
  u8 res;
  asm volatile("inb %w1, %b0" : "=a"(res) : "Nd"(port) : "memory");
  return res;
}

static inline u16 inw(u16 port) {
  u16 res;
  asm volatile("inw %w1, %w0" : "=a"(res) : "Nd"(port) : "memory");
  return res;
}

static inline u32 inl(u16 port) {
  u32 res;
  asm volatile("inl %w1, %0" : "=a"(res) : "Nd"(port) : "memory");
  return res;
}

static inline void cli(void) {
  asm volatile("cli");
}

static inline void sti(void) {
  asm volatile("sti");
}

static inline void hlt(void) {
  asm volatile("hlt");
}

static inline u64 read_rflags(void) {
  u64 rflags;
  asm volatile("pushfq; popq %0" : "=r"(rflags));
  return rflags;
}

static inline u8 xchg(volatile u8 *addr, u8 newval) {
  u8 result = newval;
  asm volatile("lock; xchgb %0, %1"
               : "+m"(*addr), "+r"(result)
               :
               : "cc");
  return result;
}

#endif
