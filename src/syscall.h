#ifndef SYSCALL_H
#define SYSCALL_H
#include "types.h"

#define SYS_EXIT   0
#define SYS_WRITE  1
#define SYS_GETPID 2

// MSR addresses
#define MSR_EFER  0xC0000080
#define MSR_STAR  0xC0000081
#define MSR_LSTAR 0xC0000082
#define MSR_FMASK 0xC0000084

#define EFER_SCE  (1UL << 0)  // syscall enable

void init_syscall(void);

#endif
