#include "syscall.h"
#include "gdt.h"
#include "mem.h"
#include "print.h"
#include "proc.h"
#include "x86.h"

// Kernel stack for syscall entry (set by scheduler)
u64 kernel_rsp;

// Scratch space for saving user RSP during syscall entry
u64 scratch_rsp;

extern void syscall_entry(void);

void init_syscall(void) {
    // Enable SCE (syscall enable) in EFER
    u64 efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    // STAR: bits 47:32 = kernel CS (syscall), bits 63:48 = user base (sysret)
    // syscall: CS = STAR[47:32], SS = STAR[47:32]+8
    // sysretq: CS = STAR[63:48]+16 | 3, SS = STAR[63:48]+8 | 3
    u64 star = ((u64)0x30 << 48) | ((u64)KERNEL_CS << 32);
    wrmsr(MSR_STAR, star);

    // LSTAR: syscall entry point
    wrmsr(MSR_LSTAR, (u64)syscall_entry);

    // FMASK: clear IF on syscall (disable interrupts during entry)
    wrmsr(MSR_FMASK, 0x200);
}

static i64 sys_write(u64 fd, const char *buf, u64 len) {
    if (fd != 1) return -1;
    // Basic user pointer validation
    if ((u64)buf >= 0x800000000000UL) return -1;
    for (u64 i = 0; i < len; i++)
        putc(buf[i]);
    return (i64)len;
}

static i64 sys_getpid(void) {
    if (!current_proc) return -1;
    return (i64)current_proc->pid;
}

static void sys_exit(void) {
    if (!current_proc) return;
    printf("proc: pid %u exited\r\n", current_proc->pid);
    current_proc->state = PROC_UNUSED;
    // Yield to scheduler; this process will never be resumed
    struct context **ctx = cpu_context_ptr();
    swtch(&current_proc->context, *ctx);
}

// Called from syscall_entry.S
// Args follow Linux syscall convention remapped to C calling convention
i64 syscall_handler(u64 num, u64 a1, u64 a2, u64 a3, u64 a4, u64 a5) {
    (void)a4; (void)a5;
    switch (num) {
    case SYS_EXIT:
        sys_exit();
        return 0;  // never reached
    case SYS_WRITE:
        return sys_write(a1, (const char *)a2, a3);
    case SYS_GETPID:
        return sys_getpid();
    default:
        return -1;
    }
}
