#ifndef PANIC_H
#define PANIC_H
#include "idt.h"

#define panic(...) __panic_impl(__VA_ARGS__, NULL, NULL)
#define __panic_impl(msg, frame, ...) __panic(msg, frame)

__attribute__((noreturn)) void __panic(const char *msg, struct trap_frame *frame);

#endif
