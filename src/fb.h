#ifndef FB_H
#define FB_H
#include "types.h"
#include "limine.h"

void fb_init(struct limine_framebuffer *fb);
void fb_putc(char c);
void fb_clear(void);

#endif
