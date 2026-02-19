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

void pci_write8(u8 bus, u8 slot, u8 func, u8 offset, u8 val) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    u32 data = inl(PCI_CONFIG_DATA);
    int shift = (offset & 3) * 8;
    data &= ~(0xFF << shift);
    data |= (u32)val << shift;
    outl(PCI_CONFIG_DATA, data);
}

void pci_write16(u8 bus, u8 slot, u8 func, u8 offset, u16 val) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    u32 data = inl(PCI_CONFIG_DATA);
    int shift = (offset & 2) * 8;
    data &= ~(0xFFFF << shift);
    data |= (u32)val << shift;
    outl(PCI_CONFIG_DATA, data);
}

void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val) {
    u32 address = PCI_ADDR(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDR, address);
    outl(PCI_CONFIG_DATA, val);
}

// Read a BAR, handling 64-bit BARs (returns physical address)
u64 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar) {
    u8 offset = PCI_BAR0_OFFSET + bar * 4;
    u32 low = pci_read32(bus, slot, func, offset);

    // Check BAR type (bits 2:1)
    // 00 = 32-bit, 10 = 64-bit
    if ((low & PCI_BAR_TYPE_MASK) == PCI_BAR_TYPE_64BIT) {
        // 64-bit BAR: read high 32 bits from next BAR
        u32 high = pci_read32(bus, slot, func, offset + 4);
        return ((u64)high << 32) | (low & PCI_BAR_ADDR_MASK);
    }
    return low & PCI_BAR_ADDR_MASK;  // Mask off type bits
}

// Find capability in capability list, returns offset or 0 if not found
u8 pci_find_cap(u8 bus, u8 slot, u8 func, u8 cap_id) {
    // Check if device has capabilities
    u16 status = pci_read16(bus, slot, func, PCI_STATUS_OFFSET);
    if (!(status & PCI_STATUS_CAP_LIST))
        return 0;

    // Get capabilities pointer
    u8 ptr = pci_read8(bus, slot, func, PCI_CAP_PTR_OFFSET) & ~0x3;

    // Walk the list
    while (ptr) {
        u8 id = pci_read8(bus, slot, func, ptr);
        if (id == cap_id)
            return ptr;
        ptr = pci_read8(bus, slot, func, ptr + 1) & ~0x3;
    }

    return 0;
}

// Enable MSI for a device, returns 0 on success
int pci_msi_enable(struct pci_device *dev, u8 vector) {
    u8 cap = pci_find_cap(dev->bus, dev->slot, dev->func, PCI_CAP_MSI);
    if (!cap)
        return -1;

    // Read message control
    u16 ctrl = pci_read16(dev->bus, dev->slot, dev->func, cap + 2);

    // MSI address format for x86: MSI_ADDR_BASE | (dest_apic << 12)
    // We send to LAPIC 0, edge triggered, fixed delivery
    u32 addr = MSI_ADDR_BASE;  // Dest APIC 0

    // Write address
    pci_write32(dev->bus, dev->slot, dev->func, cap + 4, addr);

    // If 64-bit capable, write upper address and adjust data offset
    int data_off = cap + 8;
    if (ctrl & MSI_CTRL_64BIT) {
        pci_write32(dev->bus, dev->slot, dev->func, cap + 8, 0);
        data_off = cap + 12;
    }

    // Write data (vector number)
    pci_write16(dev->bus, dev->slot, dev->func, data_off, vector);

    // Enable MSI (1 message)
    ctrl &= ~MSI_CTRL_MME_MASK;  // Clear MME (use 1 vector)
    ctrl |= MSI_CTRL_ENABLE;
    pci_write16(dev->bus, dev->slot, dev->func, cap + 2, ctrl);

    return 0;
}

void pci_msi_disable(struct pci_device *dev) {
    u8 cap = pci_find_cap(dev->bus, dev->slot, dev->func, PCI_CAP_MSI);
    if (!cap)
        return;

    u16 ctrl = pci_read16(dev->bus, dev->slot, dev->func, cap + 2);
    ctrl &= ~MSI_CTRL_ENABLE;
    pci_write16(dev->bus, dev->slot, dev->func, cap + 2, ctrl);
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
