#include "gdt.h"
#include "mem.h"

struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

struct tss_descriptor {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
    u32 base_upper;
    u32 reserved;
} __attribute__((packed));

struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

// GDT: 9 regular entries + 1 TSS (2 slots) = 11 slots worth of space
static u8 gdt_data[11 * 8] __attribute__((aligned(16)));
static struct tss tss;

static void gdt_set_entry(int idx, u32 base, u32 limit, u8 access, u8 gran) {
    struct gdt_entry *e = (struct gdt_entry *)&gdt_data[idx * 8];
    e->limit_low   = limit & 0xFFFF;
    e->base_low    = base & 0xFFFF;
    e->base_mid    = (base >> 16) & 0xFF;
    e->access      = access;
    e->granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    e->base_high   = (base >> 24) & 0xFF;
}

static void gdt_set_tss(int idx, u64 base, u32 limit) {
    struct tss_descriptor *d = (struct tss_descriptor *)&gdt_data[idx * 8];
    d->limit_low   = limit & 0xFFFF;
    d->base_low    = base & 0xFFFF;
    d->base_mid    = (base >> 16) & 0xFF;
    d->access      = 0x89;  // present, 64-bit TSS (available)
    d->granularity = (limit >> 16) & 0x0F;
    d->base_high   = (base >> 24) & 0xFF;
    d->base_upper  = (base >> 32) & 0xFFFFFFFF;
    d->reserved    = 0;
}

void init_gdt(void) {
    memset(gdt_data, 0, sizeof(gdt_data));
    memset(&tss, 0, sizeof(tss));
    tss.iopb_offset = sizeof(tss);

    // Replicate Limine's GDT layout, then add user segments + TSS
    // [0] null
    gdt_set_entry(0, 0, 0, 0, 0);
    // [1] 0x08: 16-bit code
    gdt_set_entry(1, 0, 0xFFFF, 0x9A, 0x00);
    // [2] 0x10: 16-bit data
    gdt_set_entry(2, 0, 0xFFFF, 0x92, 0x00);
    // [3] 0x18: 32-bit code
    gdt_set_entry(3, 0, 0xFFFFF, 0x9A, 0xCF);
    // [4] 0x20: 32-bit data
    gdt_set_entry(4, 0, 0xFFFFF, 0x92, 0xCF);
    // [5] 0x28: 64-bit kernel code (KERNEL_CS)
    gdt_set_entry(5, 0, 0xFFFFF, 0x9A, 0xAF);
    // [6] 0x30: 64-bit kernel data (KERNEL_DS)
    gdt_set_entry(6, 0, 0xFFFFF, 0x92, 0xAF);
    // [7] 0x38: 64-bit user data (DPL=3)
    gdt_set_entry(7, 0, 0xFFFFF, 0xF2, 0xAF);
    // [8] 0x40: 64-bit user code (DPL=3)
    gdt_set_entry(8, 0, 0xFFFFF, 0xFA, 0xAF);
    // [9-10] 0x48: TSS descriptor (16 bytes)
    gdt_set_tss(9, (u64)&tss, sizeof(tss) - 1);

    // Load GDT
    struct gdt_ptr gdtr;
    gdtr.limit = sizeof(gdt_data) - 1;
    gdtr.base = (u64)&gdt_data;
    asm volatile("lgdt %0" : : "m"(gdtr));

    // Reload CS via far return
    asm volatile(
        "pushq $0x28\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        ::: "rax", "memory"
    );

    // Reload data segments
    asm volatile(
        "movw $0x30, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        "movw $0x00, %%ax\n"
        "movw %%ax, %%fs\n"
        "movw %%ax, %%gs\n"
        ::: "rax"
    );

    // Load TSS
    asm volatile("ltr %w0" : : "r"((u16)TSS_SEL));
}

void tss_set_rsp0(u64 rsp0) {
    tss.rsp0 = rsp0;
}
