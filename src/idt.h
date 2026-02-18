#ifndef IDT_H
#define IDT_H
#include "types.h"

#define IDT_INTERRUPT_GATE 0x8E  // present, ring 0, interrupt gate
#define IDT_TRAP_GATE      0x8F  // present, ring 0, trap gate
#define IDT_ENTRIES        256

struct idt_entry {
  u16	offset_1;
  u16	selector;
  u8	ist;
  u8	type_attr;
  u16	offset_2;
  u32	offset_3;
  u32	zero;			//reserved
} __attribute__((packed));

struct idt_ptr {
  u16 limit;
  u64 base;
} __attribute__((packed));

struct trap_frame {
  u64 r15, r14, r13, r12, r11, r10, r9, r8;
  u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
  u64 int_no, error_code;
  u64 rip, cs, rflags, rsp, ss;
};

void exception_handler(struct trap_frame *frame);

void init_idt(void);
void idt_set_gate(u8 num, u64 handler, u8 type);

#endif
