#include "acpi.h"
#include "apic.h"
#include "idt.h"
#include "limine.h"
#include "mem.h"
#include "print.h"
#include "serial.h"
#include "types.h"
#include "x86.h"
#include "pci.h"
#include "ahci.h"
#include "fat.h"

static volatile u64 limine_base_revision[] = LIMINE_BASE_REVISION(4);

static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID, .revision = 0};

static volatile struct limine_memmap_request memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID, .revision = 0};

static volatile struct limine_mp_request mp_request = {
    .id = LIMINE_MP_REQUEST_ID, .revision = 0, .flags = 0};

static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID, .revision = 0};

static volatile struct limine_rsdp_request rsdp_request = {
    .id = LIMINE_RSDP_REQUEST_ID, .revision = 0};

static volatile u64 ap_started = 0;

void ap_entry(struct limine_mp_info *info) {
    (void)info;
    __sync_fetch_and_add(&ap_started, 1);

    // AP halts for now
    for (;;) {
        cli();
        hlt();
    }
}

void kmain(void) {
    init_idt();
    init_serial();
    puts("[ OK ] Kernel starting!\r\n");

    // Initialize memory allocator with HHDM offset
    kinit(hhdm_request.response->offset);

    // Process memory map and free usable memory
    struct limine_memmap_response *memmap_response = memmap_request.response;
    struct limine_memmap_entry **entries = memmap_response->entries;
    u64 available_mem = 0;

    for (u64 i = 0; i < memmap_response->entry_count; i++) {
        struct limine_memmap_entry *entry = entries[i];
        if (entry->type == LIMINE_MEMMAP_USABLE) {
            freerange(entry->base, entry->base + entry->length);
            available_mem += entry->length;
        }
    }

    puts("[ OK ] Memory initialized\r\n");

    // Start APs (Application Processors)
    struct limine_mp_response *mp_response = mp_request.response;
    if (mp_response) {
        u64 cpu_count = mp_response->cpu_count;
        u32 bsp_lapic_id = mp_response->bsp_lapic_id;

        for (u64 i = 0; i < cpu_count; i++) {
            struct limine_mp_info *cpu = mp_response->cpus[i];

            // Skip BSP (bootstrap processor)
            if (cpu->lapic_id == bsp_lapic_id)
                continue;

            // Set the entry point - this wakes up the AP
            __atomic_store_n(&cpu->goto_address, ap_entry, __ATOMIC_SEQ_CST);
        }

        // Wait for all APs to start
        u64 expected_aps = cpu_count - 1;
        while (__atomic_load_n(&ap_started, __ATOMIC_SEQ_CST) < expected_aps)
            ;

        puts("[ OK ] All CPUs started\r\n");
    }

    puts("Starting ACPI...\r\n");
    init_acpi(rsdp_request.response->address);
    puts("ACPI done, disabling PIC...\r\n");
    pic_disable();
    puts("PIC disabled, starting LAPIC...\r\n");
    lapic_init();
    puts("LAPIC done, starting IOAPIC...\r\n");
    ioapic_init();
    puts("IOAPIC done\r\n");

    // Draw something on framebuffer
    struct limine_framebuffer *framebuffer =
        framebuffer_request.response->framebuffers[0];
    for (u32 i = 0; i < 100; i++) {
        volatile u32 *fb_ptr = framebuffer->address;
        fb_ptr[1 * (framebuffer->width) + i] = 0xFFFFFFFF;
    }

    puts("[ OK ] Kernel initialized!\r\n");

    printf("LAPIC ID: %u\r\n", lapic_id());

    // Route timer (IRQ 0) to test if IOAPIC works at all
    ioapic_route_irq(0, 32, lapic_id());
    pit_stop();
    lapic_timer_periodic(32, 1000000);
    ioapic_route_irq(1, 33, lapic_id());

    // Enable keyboard interrupts on PS/2 controller
    while (inb(0x64) & 0x02);  // wait for input buffer empty
    outb(0x64, 0x20);          // read controller config
    while (!(inb(0x64) & 0x01)); // wait for output buffer full
    u8 config = inb(0x60);
    config |= 0x01;            // enable keyboard interrupt
    while (inb(0x64) & 0x02);  // wait for input buffer empty
    outb(0x64, 0x60);          // write controller config
    while (inb(0x64) & 0x02);
    outb(0x60, config);

    puts("Keyboard enabled\r\n");

    pci_scan();

    ahci_init();

    fat_init();

    sti();

    puts("Waiting for keyboard...\r\n");

    for (;;) {
        hlt();
    }
}
