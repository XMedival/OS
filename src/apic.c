#include "apic.h"
#include "acpi.h"
#include "mem.h"
#include "x86.h"
#include "serial.h"

static volatile u32 *lapic_base;
static volatile u32 *ioapic_base;

// Local APIC read/write
static inline u32 lapic_read(u32 reg) {
    return lapic_base[reg / 4];
}

static inline void lapic_write(u32 reg, u32 val) {
    lapic_base[reg / 4] = val;
}

// I/O APIC read/write
static inline u32 ioapic_read(u32 reg) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    return ioapic_base[IOAPIC_REGWIN / 4];
}

static inline void ioapic_write(u32 reg, u32 val) {
    ioapic_base[IOAPIC_REGSEL / 4] = reg;
    ioapic_base[IOAPIC_REGWIN / 4] = val;
}

void pic_disable(void) {
    // Mask all IRQs on both PICs
    outb(0xA1, 0xFF);  // Slave PIC
    outb(0x21, 0xFF);  // Master PIC
}

void lapic_init(void) {
    struct MADT *madt = acpi_tables.madt;
    if (!madt) {
        serial_puts("LAPIC: No MADT found!\r\n");
        return;
    }
    serial_puts("LAPIC: MADT found\r\n");

    // Get LAPIC base from MADT
    lapic_base = PHYS_TO_VIRT((u64)madt->local_apic_addr);
    serial_puts("LAPIC: base set\r\n");

    // Check for LAPIC address override in MADT entries
    // Entries start at offset 44: header(36) + local_apic_addr(4) + flags(4)
    u8 *ptr = (u8 *)madt + 44;
    u8 *end = (u8 *)madt + madt->h.length;

    while (ptr < end) {
        u8 type = ptr[0];
        u8 len = ptr[1];

        if (len == 0) break;  // prevent infinite loop

        if (type == MADT_LAPIC_OVERRIDE) {
            u64 addr = *(u64 *)(ptr + 4);
            lapic_base = PHYS_TO_VIRT(addr);
        }
        ptr += len;
    }
    serial_puts("LAPIC: parsed MADT entries\r\n");

    // Map LAPIC MMIO region
    serial_puts("LAPIC: mapping MMIO...\r\n");
    map_mmio((u64)madt->local_apic_addr, PAGE_SIZE);

    // Enable LAPIC via Spurious Vector Register
    // Vector 0xFF for spurious interrupts, set enable bit
    serial_puts("LAPIC: writing SVR...\r\n");
    lapic_write(LAPIC_SVR, LAPIC_SVR_ENABLE | 0xFF);
    serial_puts("LAPIC: SVR done\r\n");

    // Set task priority to 0 (accept all interrupts)
    lapic_write(LAPIC_TPR, 0);

    serial_puts("[ OK ] LAPIC initialized\r\n");
}

void lapic_eoi(void) {
    lapic_write(LAPIC_EOI, 0);
}

u32 lapic_id(void) {
    return lapic_read(LAPIC_ID) >> 24;
}

void ioapic_init(void) {
    struct MADT *madt = acpi_tables.madt;
    if (!madt) {
        serial_puts("IOAPIC: No MADT found!\r\n");
        return;
    }

    // Parse MADT entries to find I/O APIC
    // Entries start at offset 44: header(36) + local_apic_addr(4) + flags(4)
    u8 *ptr = (u8 *)madt + 44;
    u8 *end = (u8 *)madt + madt->h.length;

    while (ptr < end) {
        u8 type = ptr[0];
        u8 len = ptr[1];

        if (type == MADT_IOAPIC) {
            // Debug: print raw bytes
            serial_puts("IOAPIC entry bytes: ");
            for (int i = 0; i < 12; i++) {
                u8 b = ptr[i];
                serial_putc("0123456789ABCDEF"[(b >> 4) & 0xF]);
                serial_putc("0123456789ABCDEF"[b & 0xF]);
                serial_putc(' ');
            }
            serial_puts("\r\n");

            // IOAPIC entry: type(1) len(1) id(1) reserved(1) addr(4) gsi_base(4)
            u32 addr = ptr[4] | (ptr[5] << 8) | (ptr[6] << 16) | (ptr[7] << 24);
            serial_puts("IOAPIC: phys addr 0x");
            for (int i = 7; i >= 0; i--) {
                u8 nibble = (addr >> (i * 4)) & 0xF;
                serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
            }
            serial_puts("\r\n");
            map_mmio((u64)addr, PAGE_SIZE);
            ioapic_base = PHYS_TO_VIRT((u64)addr);
            break;
        }
        ptr += len;
    }

    if (!ioapic_base) {
        serial_puts("IOAPIC: Not found in MADT!\r\n");
        return;
    }

    // Mask all IRQs initially
    u32 max_redir = (ioapic_read(IOAPIC_VER) >> 16) & 0xFF;
    for (u32 i = 0; i <= max_redir; i++) {
        ioapic_write(IOAPIC_REDTBL + i * 2, 0x10000);     // masked
        ioapic_write(IOAPIC_REDTBL + i * 2 + 1, 0);
    }

    serial_puts("[ OK ] IOAPIC initialized\r\n");
}

// Translate ISA IRQ to GSI (checks for Interrupt Source Overrides)
static u32 irq_to_gsi(u8 irq) {
    struct MADT *madt = acpi_tables.madt;
    if (!madt) return irq;

    // Entries start at offset 44
    u8 *ptr = (u8 *)madt + 44;
    u8 *end = (u8 *)madt + madt->h.length;

    while (ptr < end) {
        u8 type = ptr[0];
        u8 len = ptr[1];
        if (len == 0) break;

        // Type 2 = Interrupt Source Override
        if (type == MADT_ISO) {
            u8 source_irq = ptr[3];
            u32 gsi = *(u32 *)(ptr + 4);
            if (source_irq == irq) {
                serial_puts("IRQ override: ");
                serial_putc('0' + irq);
                serial_puts(" -> GSI ");
                serial_putc('0' + (gsi / 10));
                serial_putc('0' + (gsi % 10));
                serial_puts("\r\n");
                return gsi;
            }
        }
        ptr += len;
    }
    return irq;  // no override, GSI == IRQ
}

void ioapic_route_irq(u8 irq, u8 vector, u8 dest_lapic_id) {
    u32 gsi = irq_to_gsi(irq);
    u32 lo = vector;                      // vector number
    u32 hi = (u32)dest_lapic_id << 24;    // destination APIC ID

    serial_puts("IOAPIC: routing GSI ");
    serial_putc('0' + gsi);
    serial_puts(" to vec ");
    serial_putc('0' + (vector / 10));
    serial_putc('0' + (vector % 10));
    serial_puts(" LAPIC ");
    serial_putc('0' + dest_lapic_id);
    serial_puts("\r\n");

    ioapic_write(IOAPIC_REDTBL + gsi * 2 + 1, hi);  // write hi first
    ioapic_write(IOAPIC_REDTBL + gsi * 2, lo);

    // Read back and verify both
    u32 lo_read = ioapic_read(IOAPIC_REDTBL + gsi * 2);
    u32 hi_read = ioapic_read(IOAPIC_REDTBL + gsi * 2 + 1);
    serial_puts("IOAPIC: lo=0x");
    for (int i = 7; i >= 0; i--) {
        u8 nibble = (lo_read >> (i * 4)) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts(" hi=0x");
    for (int i = 7; i >= 0; i--) {
        u8 nibble = (hi_read >> (i * 4)) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
    serial_puts("\r\n");
}

void ioapic_mask_irq(u8 irq) {
    u32 gsi = irq_to_gsi(irq);
    u32 lo = ioapic_read(IOAPIC_REDTBL + gsi * 2);
    lo |= (1 << 16);
    ioapic_write(IOAPIC_REDTBL + gsi * 2, lo);
}

void ioapic_unmask_irq(u8 irq) {
    u32 gsi = irq_to_gsi(irq);
    u32 lo = ioapic_read(IOAPIC_REDTBL + gsi * 2);
    lo &= ~(1 << 16);
    ioapic_write(IOAPIC_REDTBL + gsi * 2, lo);
}

// PIT (Programmable Interval Timer)
#define PIT_FREQ 1193182
#define PIT_CMD  0x43
#define PIT_CH0  0x40

void pit_init(u32 hz) {
    u16 divisor = PIT_FREQ / hz;
    outb(PIT_CMD, 0x36);            // channel 0, lobyte/hibyte, square wave
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
}

void pit_stop(void) {
    outb(PIT_CMD, 0x30);            // channel 0, lobyte/hibyte, mode 0
    outb(PIT_CH0, 0);
    outb(PIT_CH0, 0);
}

// LAPIC Timer
#define LAPIC_TIMER_PERIODIC 0x20000
#define LAPIC_TIMER_MASKED   0x10000

void lapic_timer_init(u8 vector, u32 initial_count) {
    lapic_write(LAPIC_TIMER_DIV, 0x3);      // divide by 16
    lapic_write(LAPIC_TIMER, vector);        // one-shot mode
    lapic_write(LAPIC_TIMER_INIT, initial_count);
}

void lapic_timer_periodic(u8 vector, u32 initial_count) {
    lapic_write(LAPIC_TIMER_DIV, 0x3);      // divide by 16
    lapic_write(LAPIC_TIMER, vector | LAPIC_TIMER_PERIODIC);
    lapic_write(LAPIC_TIMER_INIT, initial_count);
}

void lapic_timer_stop(void) {
    lapic_write(LAPIC_TIMER, LAPIC_TIMER_MASKED);
    lapic_write(LAPIC_TIMER_INIT, 0);
}
