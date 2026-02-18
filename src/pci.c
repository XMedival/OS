#include "pci.h"
#include "print.h"

struct pci_device pci_devices[MAX_PCI_DEVICES];
u32 pci_device_count = 0;

u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return (inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF;
}

u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return (inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF;
}

u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    return inl(PCI_CONFIG_DATA);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, val);
}

// Read a BAR, handling 64-bit BARs (returns physical address)
u64 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar) {
    u8 offset = 0x10 + bar * 4;
    u32 low = pci_read32(bus, slot, func, offset);

    // Check BAR type (bits 2:1)
    // 00 = 32-bit, 10 = 64-bit
    if ((low & 0x6) == 0x4) {
        // 64-bit BAR: read high 32 bits from next BAR
        u32 high = pci_read32(bus, slot, func, offset + 4);
        return ((u64)high << 32) | (low & ~0xFULL);
    }
    return low & ~0xFULL;  // Mask off type bits
}

void pci_scan(void) {
    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 slot = 0; slot < 32; slot++) {
            u16 vendor = pci_read16(bus, slot, 0, 0);
            if (vendor == 0xFFFF)
                continue;

            struct pci_device *dev = &pci_devices[pci_device_count++];
            dev->bus = bus;
            dev->slot = slot;
            dev->func = 0;
            dev->vendor_id = vendor;
            dev->device_id = pci_read16(bus, slot, 0, 2);
            dev->class_code = pci_read8(bus, slot, 0, 0x0B);
            dev->subclass = pci_read8(bus, slot, 0, 0x0A);
            printf("[ PCI ] %u:%u vendor=%X device=%X class=%x:%x\r\n",
                   bus, slot, dev->vendor_id, dev->device_id,
                   dev->class_code, dev->subclass);
            }
        }
}    
