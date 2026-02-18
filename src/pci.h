#ifndef PCI_H
#define PCI_H
#include "types.h"
#include "x86.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_ADDR(bus, slot, func, off)                                         \
    (0x80000000 | ((u32)(bus) << 16) | ((u32)(slot) << 11) |                   \
     ((u32)(func) << 8) | ((off) & 0xFC))

#define PCI_CLASS_STORAGE 0x01
#define PCI_CLASS_NETWORK 0x02
#define PCI_CLASS_DISPLAY 0x03
#define PCI_CLASS_BRIDGE 0x06

#define MAX_PCI_DEVICES 256

struct pci_common_header {
    u16 vendor_id;
    u16 device_id;
    u16 command;
    u16 status;
    u8 revision;
    u8 prog_if;
    u8 subclass;
    u8 class_code;
    u8 cache_line_size;
    u8 latency_timer;
    u8 header_type;
    u8 bist;
} __attribute__((packed));

struct pci_general_header {
    struct pci_common_header h;
    u32 bar0;
    u32 bar1;
    u32 bar2;
    u32 bar3;
    u32 bar4;
    u32 bar5;
    u32 cis_ptr;
    u16 sub_vendor_id;
    u16 subsystem_id;
    u32 expansion_addr;
    u8 capabilities_ptr;
    u8 _[3];
    u32 __;
    u8 int_line;
    u8 int_pin;
    u8 min_grant;
    u8 max_latency;
} __attribute__((packed));

struct pci_bridge_header {
    struct pci_common_header h;
    u32 bar0;
    u32 bar1;
    u8 primary_bus_num;
    u8 secondary_bus_num;
    u8 subordinate_bus_num;
    u8 secondary_latency_timer;
    u8 io_base;
    u8 io_limit;
    u16 secondary_status;
    u16 memory_base;
    u16 memory_limit;
    u16 prefetchable_mem_base;
    u16 prefetchable_mem_limit;
    u32 prefetchable_mem_base32;
    u32 prefetchable_mem_limit32;
    u16 io_base_upper;
    u16 io_limit_upper;
    u8 capabilities_ptr;
    u8 _[3];
    u32 expansion_addr;
    u8 int_line;
    u8 int_pin;
    u16 bridge_control;
} __attribute__((packed));

struct pci_device {
    u8 bus, slot, func;
    u16 vendor_id, device_id;
    u8 class_code, subclass;
    u8 header_type;
    u8 int_line;
};

extern struct pci_device pci_devices[MAX_PCI_DEVICES];
extern u32 pci_device_count;

u8 pci_read8(u8 bus, u8 slot, u8 func, u8 offset);
u16 pci_read16(u8 bus, u8 slot, u8 func, u8 offset);
u32 pci_read32(u8 bus, u8 slot, u8 func, u8 offset);
void pci_write32(u8 bus, u8 slot, u8 func, u8 offset, u32 val);
u64 pci_read_bar(u8 bus, u8 slot, u8 func, u8 bar);  // bar = 0-5
void pci_scan(void);

// PCI subclass for storage controllers
#define PCI_SUBCLASS_AHCI  0x06  // SATA AHCI
#define PCI_SUBCLASS_NVME  0x08  // NVMe

#endif
