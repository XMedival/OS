#ifndef PRINT_H
#define PRINT_H
#include "types.h"

void putc(char c);
void puts(const char *s);
void putdec(u64 n);
void puthex8(u8 n);
void puthex16(u16 n);
void puthex32(u32 n);
void puthex64(u64 n);
void puthex(u64 n);  // auto-width

// printf-lite (supports %d, %u, %x, %s, %c, %p)
void printf(const char *fmt, ...);

#endif
