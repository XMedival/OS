#ifndef SERIAL_H
#define SERIAL_H
#include "types.h"
#include "x86.h"

#define PORT 0x3F8

static inline void init_serial(void) {
  outb(PORT + 1, 0x00);
  outb(PORT + 3, 0x80);
  outb(PORT + 0, 0x03);
  outb(PORT + 1, 0x00);
  outb(PORT + 3, 0x03);
  outb(PORT + 2, 0xC7);
  outb(PORT + 4, 0x0B);
  outb(PORT + 4, 0x1E);
  outb(PORT + 0, 0xAE);
  outb(PORT + 4, 0x0F);
}

static inline u32 is_transmit_empty(void) {
  return inb(PORT + 5) & 0x20;
}

static inline void serial_putc(char c) {
  while(is_transmit_empty() == 0);
  outb(PORT, c);
}

static inline void serial_puts(char* s) {
  for (u32 i = 0;;i++) {
    if (s[i] == '\0')
      break;
    serial_putc(s[i]);
  }
}

#endif
