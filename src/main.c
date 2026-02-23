#include "acpi.h"
#include "apic.h"
#include "ata.h"
#include "blk.h"
#include "fb.h"
#include "gdt.h"
#include "idt.h"
#include "limine.h"
#include "mem.h"
#include "panic.h"
#include "print.h"
#include "proc.h"
#include "ps2.h"
#include "serial.h"
#include "syscall.h"
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

    init_gdt();
    puts("[ OK ] GDT initialized\r\n");

    init_syscall();
    puts("[ OK ] Syscalls initialized\r\n");

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

    // Initialize framebuffer console (before any puts so output appears on screen)
    fb_init(framebuffer_request.response->framebuffers[0]);

    puts("[ OK ] Kernel initialized!\r\n");

    printf("LAPIC ID: %u\r\n", lapic_id());

    // Route timer (IRQ 0) to test if IOAPIC works at all
    ioapic_route_irq(0, 32, lapic_id());
    pit_stop();
    lapic_timer_periodic(32, 1000000);
    ioapic_route_irq(1, 33, lapic_id());
    ioapic_route_irq(12, 44, lapic_id());   // PS/2 mouse IRQ
    ioapic_route_irq(14, 46, lapic_id());   // ATA primary IRQ
    ioapic_route_irq(15, 47, lapic_id());   // ATA secondary IRQ

    ps2_init();

    sti();

    pci_scan();

    ahci_init();
    // ata is currently broken, hangs on the second read
    // ata_init();

    struct blk_device *boot_dev = blk_get("ahci0");
    if (!boot_dev) boot_dev = blk_get("ata0");
    if (!boot_dev) panic("CANNOT GET A DRIVER FOR THE BOOT DEVICE");
    fat_init(boot_dev);

    puts("Waiting for keyboard...\r\n");

    // Create processes from ELF files on disk
    proc_create("/test.elf");
    proc_create("/test.elf");

    puts("[ OK ] Entering scheduler\r\n");
    scheduler();
    // scheduler never returns
}
