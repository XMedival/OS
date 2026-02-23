#ifndef PROC_H
#define PROC_H
#include "types.h"

#define MAX_PROCS    64
#define KSTACK_SIZE  (4096 * 2)  // 8KB kernel stack
#define USER_STACK_TOP  0x7FFFFFF000UL
#define USER_STACK_BASE 0x7FFFFFE000UL

// Process states
#define PROC_UNUSED   0
#define PROC_RUNNABLE 1
#define PROC_RUNNING  2

// Saved by swtch(), restored when switching to a process
struct context {
    u64 r15;
    u64 r14;
    u64 r13;
    u64 r12;
    u64 rbx;
    u64 rbp;
    u64 rip;
};

struct trap_frame;

struct proc {
    u32 pid;
    u32 state;
    u64 *pml4;              // page table (virtual address)
    u8  *kstack;            // kernel stack base (virtual)
    struct trap_frame *tf;  // trap frame on kernel stack
    struct context *context;
    char name[16];
};

// Assembly context switch: saves old context, loads new
void swtch(struct context **old, struct context *new_ctx);

// Scheduler (called from kmain, never returns)
void scheduler(void);

// Yield current process (called from timer interrupt)
void yield(void);

// Create a process from an ELF file on FAT
struct proc *proc_create(const char *path);

// The current running process (NULL if in scheduler)
extern struct proc *current_proc;

// Get scheduler context pointer (for syscall exit path)
struct context **cpu_context_ptr(void);

#endif
