#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2

static inline long syscall0(long num) {
    long ret;
    asm volatile("syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory");
    return ret;
}

static inline long syscall3(long num, long a1, long a2, long a3) {
    long ret;
    register long r10 asm("r10") = 0;
    register long r8 asm("r8") = 0;
    asm volatile("syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory");
    return ret;
}

static inline void exit(void) {
    syscall0(SYS_EXIT);
    for (;;);  // unreachable
}

static inline long write(int fd, const void *buf, unsigned long len) {
    return syscall3(SYS_WRITE, fd, (long)buf, (long)len);
}

static inline long getpid(void) {
    return syscall0(SYS_GETPID);
}

#endif
