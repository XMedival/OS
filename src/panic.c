#include "panic.h"
#include "print.h"
#include "x86.h"

void panic(const char *msg, struct trap_frame *frame) {
    if (msg || frame)
        puts("======================== PANIC "
             "========================\r\n");
    if (msg) {
        printf("%^56s\r\n", msg);
    }
    if (frame) {
        printf("EXCEPTION: %p ERRNO: %p\r\n", frame->int_no, frame->error_code);
        printf("RAX:	   %p RBX:   %p\r\n", frame->rax, frame->rbx);
        printf("RCX:	   %p RDX:   %p\r\n", frame->rcx, frame->rdx);
        printf("RSP:	   %p RBP:   %p\r\n", frame->rsp, frame->rbp);
        printf("RAX:	   %p RBX:   %p\r\n", frame->rax, frame->rbx);
        puts("=============================="
             "=========================\r\n");
    }
    for (;;) {
        cli();
        hlt();
    }
}
