#ifndef APIC_H
#define APIC_H
#include "types.h"

// Local APIC registers (offsets from base)
#define LAPIC_ID         0x020
#define LAPIC_VER        0x030
#define LAPIC_TPR        0x080  // Task Priority
#define LAPIC_EOI        0x0B0  // End of Interrupt
#define LAPIC_SVR        0x0F0  // Spurious Vector
#define LAPIC_ICR_LO     0x300  // Interrupt Command (low)
#define LAPIC_ICR_HI     0x310  // Interrupt Command (high)
#define LAPIC_TIMER      0x320  // LVT Timer
#define LAPIC_TIMER_INIT 0x380  // Timer Initial Count
#define LAPIC_TIMER_CUR  0x390  // Timer Current Count
#define LAPIC_TIMER_DIV  0x3E0  // Timer Divide Config

#define LAPIC_SVR_ENABLE 0x100

// I/O APIC registers
#define IOAPIC_REGSEL 0x00
#define IOAPIC_REGWIN 0x10
#define IOAPIC_ID     0x00
#define IOAPIC_VER    0x01
#define IOAPIC_REDTBL 0x10

// MADT entry types
#define MADT_LAPIC            0
#define MADT_IOAPIC           1
#define MADT_ISO              2  // Interrupt Source Override
#define MADT_NMI              4
#define MADT_LAPIC_OVERRIDE   5

void lapic_init(void);
void ioapic_init(void);
void lapic_eoi(void);
u32 lapic_id(void);
void ioapic_route_irq(u8 irq, u8 vector, u8 dest_lapic_id);
void ioapic_mask_irq(u8 irq);
void ioapic_unmask_irq(u8 irq);
void pic_disable(void);

// Timers
void pit_init(u32 hz);
void pit_stop(void);
void lapic_timer_init(u8 vector, u32 initial_count);
void lapic_timer_periodic(u8 vector, u32 initial_count);
void lapic_timer_stop(void);

#endif
