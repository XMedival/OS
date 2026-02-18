#include "ahci.h"
#include "pci.h"
#include "mem.h"
#include "print.h"
#include "x86.h"

// Global state
static struct pci_device *ahci_dev;
static struct hba_mem *hba;

// Per-port allocated structures (32 ports max)
static struct hba_cmd_header *cmd_lists[32];
static struct hba_received_fis *fis_areas[32];
static struct hba_cmd_table *cmd_tables[32][32];  // [port][slot]

// ============================================================================
// Port Control
// ============================================================================

void ahci_port_stop(struct hba_port *port) {
    // Clear ST (stop command processing)
    port->cmd &= ~HBA_PORT_CMD_ST;

    // Wait for CR to clear
    while (port->cmd & HBA_PORT_CMD_CR)
        ;

    // Clear FRE
    port->cmd &= ~HBA_PORT_CMD_FRE;

    // Wait for FR to clear
    while (port->cmd & HBA_PORT_CMD_FR)
        ;
}

void ahci_port_start(struct hba_port *port) {
    // Wait for CR to clear
    while (port->cmd & HBA_PORT_CMD_CR)
        ;

    // Enable FRE and ST
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
}

void ahci_port_reset(struct hba_port *port) {
    ahci_port_stop(port);

    // Issue COMRESET via SCTL
    port->sctl = (port->sctl & ~0xF) | 0x1;  // DET = 1 (COMRESET)

    // Wait a bit (spec says at least 1ms)
    for (volatile int i = 0; i < 100000; i++)
        ;

    port->sctl &= ~0xF;  // DET = 0 (no action)

    // Wait for device to come back
    while ((port->ssts & 0xF) != 0x3)
        ;

    // Clear errors
    port->serr = port->serr;

    ahci_port_start(port);
}

// ============================================================================
// Port Initialization
// ============================================================================

static int get_port_num(struct hba_port *port) {
    return ((u64)port - (u64)&hba->ports[0]) / sizeof(struct hba_port);
}

int ahci_port_init(struct hba_port *port) {
    int port_num = get_port_num(port);

    ahci_port_stop(port);

    // Allocate command list (1KB, 1KB aligned)
    struct hba_cmd_header *cmd_list = kalloc();
    if (!cmd_list) return -1;
    for (int i = 0; i < 4096; i++) ((u8*)cmd_list)[i] = 0;
    cmd_lists[port_num] = cmd_list;

    u64 cmd_list_phys = VIRT_TO_PHYS((u64)cmd_list);
    port->clb = (u32)cmd_list_phys;
    port->clbu = (u32)(cmd_list_phys >> 32);

    // Allocate received FIS area (256 bytes, 256 aligned)
    struct hba_received_fis *fis = kalloc();
    if (!fis) return -1;
    for (int i = 0; i < 4096; i++) ((u8*)fis)[i] = 0;
    fis_areas[port_num] = fis;

    u64 fis_phys = VIRT_TO_PHYS((u64)fis);
    port->fb = (u32)fis_phys;
    port->fbu = (u32)(fis_phys >> 32);

    // Allocate command tables (one per slot, 128-byte aligned)
    for (int slot = 0; slot < 32; slot++) {
        struct hba_cmd_table *tbl = kalloc();
        if (!tbl) return -1;
        for (int i = 0; i < 4096; i++) ((u8*)tbl)[i] = 0;
        cmd_tables[port_num][slot] = tbl;

        u64 tbl_phys = VIRT_TO_PHYS((u64)tbl);
        cmd_list[slot].ctba = (u32)tbl_phys;
        cmd_list[slot].ctbau = (u32)(tbl_phys >> 32);
    }

    // Clear pending interrupts
    port->is = port->is;
    port->serr = port->serr;

    ahci_port_start(port);
    return 0;
}

// ============================================================================
// Device Detection
// ============================================================================

int ahci_port_type(struct hba_port *port) {
    u32 ssts = port->ssts;
    u8 det = ssts & 0xF;
    u8 ipm = (ssts >> 8) & 0xF;

    if (det != 0x3) return AHCI_DEV_NULL;  // No device
    if (ipm != 0x1) return AHCI_DEV_NULL;  // Not active

    switch (port->sig) {
    case SATA_SIG_ATA:   return AHCI_DEV_SATA;
    case SATA_SIG_ATAPI: return AHCI_DEV_SATAPI;
    case SATA_SIG_SEMB:  return AHCI_DEV_SEMB;
    case SATA_SIG_PM:    return AHCI_DEV_PM;
    default:             return AHCI_DEV_NULL;
    }
}

// ============================================================================
// Command Slot Management
// ============================================================================

int ahci_find_slot(struct hba_port *port) {
    u32 slots = port->sact | port->ci;
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i)))
            return i;
    }
    return -1;
}

int ahci_issue(struct hba_port *port, int slot) {
    // Wait for port to be ready
    while (port->tfd & (HBA_PORT_TFD_BSY | HBA_PORT_TFD_DRQ))
        ;

    // Issue command
    port->ci = (1 << slot);

    // Wait for completion
    while (1) {
        if (!(port->ci & (1 << slot)))
            break;
        if (port->is & (1 << 30)) {  // Task file error
            return -1;
        }
    }

    // Check for error
    if (port->is & (1 << 30))
        return -1;

    return 0;
}

// ============================================================================
// IDENTIFY Command
// ============================================================================

int ahci_identify(struct hba_port *port, void *buf) {
    int port_num = get_port_num(port);

    port->is = (u32)-1;  // Clear interrupts

    int slot = ahci_find_slot(port);
    if (slot < 0) return -1;

    struct hba_cmd_header *hdr = &cmd_lists[port_num][slot];
    hdr->cfl = sizeof(struct fis_reg_h2d) / 4;
    hdr->w = 0;  // Read
    hdr->prdtl = 1;

    struct hba_cmd_table *tbl = cmd_tables[port_num][slot];

    // Setup PRDT
    u64 buf_phys = VIRT_TO_PHYS((u64)buf);
    tbl->prdt[0].dba = (u32)buf_phys;
    tbl->prdt[0].dbau = (u32)(buf_phys >> 32);
    tbl->prdt[0].dbc = 511;  // 512 bytes - 1
    tbl->prdt[0].i = 1;

    // Setup command FIS
    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_CMD;
    fis->command = ATA_CMD_IDENTIFY;
    fis->device = 0;

    return ahci_issue(port, slot);
}

// ============================================================================
// Read/Write
// ============================================================================

int ahci_read(struct hba_port *port, u64 lba, u32 count, void *buf) {
    int port_num = get_port_num(port);

    port->is = (u32)-1;

    int slot = ahci_find_slot(port);
    if (slot < 0) return -1;

    struct hba_cmd_header *hdr = &cmd_lists[port_num][slot];
    hdr->cfl = sizeof(struct fis_reg_h2d) / 4;
    hdr->w = 0;  // Read
    hdr->prdtl = 1;

    struct hba_cmd_table *tbl = cmd_tables[port_num][slot];

    // Setup PRDT
    u64 buf_phys = VIRT_TO_PHYS((u64)buf);
    tbl->prdt[0].dba = (u32)buf_phys;
    tbl->prdt[0].dbau = (u32)(buf_phys >> 32);
    tbl->prdt[0].dbc = (count * 512) - 1;
    tbl->prdt[0].i = 1;

    // Setup command FIS
    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_CMD;
    fis->command = ATA_CMD_READ_DMA_EXT;

    fis->lba0 = (u8)lba;
    fis->lba1 = (u8)(lba >> 8);
    fis->lba2 = (u8)(lba >> 16);
    fis->lba3 = (u8)(lba >> 24);
    fis->lba4 = (u8)(lba >> 32);
    fis->lba5 = (u8)(lba >> 40);

    fis->device = ATA_DEV_LBA;
    fis->count = (u16)count;

    return ahci_issue(port, slot);
}

int ahci_write(struct hba_port *port, u64 lba, u32 count, const void *buf) {
    int port_num = get_port_num(port);

    port->is = (u32)-1;

    int slot = ahci_find_slot(port);
    if (slot < 0) return -1;

    struct hba_cmd_header *hdr = &cmd_lists[port_num][slot];
    hdr->cfl = sizeof(struct fis_reg_h2d) / 4;
    hdr->w = 1;  // Write
    hdr->prdtl = 1;

    struct hba_cmd_table *tbl = cmd_tables[port_num][slot];

    // Setup PRDT
    u64 buf_phys = VIRT_TO_PHYS((u64)buf);
    tbl->prdt[0].dba = (u32)buf_phys;
    tbl->prdt[0].dbau = (u32)(buf_phys >> 32);
    tbl->prdt[0].dbc = (count * 512) - 1;
    tbl->prdt[0].i = 1;

    // Setup command FIS
    struct fis_reg_h2d *fis = (struct fis_reg_h2d *)tbl->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->flags = FIS_H2D_CMD;
    fis->command = ATA_CMD_WRITE_DMA_EXT;

    fis->lba0 = (u8)lba;
    fis->lba1 = (u8)(lba >> 8);
    fis->lba2 = (u8)(lba >> 16);
    fis->lba3 = (u8)(lba >> 24);
    fis->lba4 = (u8)(lba >> 32);
    fis->lba5 = (u8)(lba >> 40);

    fis->device = ATA_DEV_LBA;
    fis->count = (u16)count;

    return ahci_issue(port, slot);
}

// ============================================================================
// Controller Initialization
// ============================================================================

void ahci_init(void) {
    // Find AHCI controller
    ahci_dev = 0;
    for (u32 i = 0; i < pci_device_count; i++) {
        struct pci_device *dev = &pci_devices[i];
        if (dev->class_code == PCI_CLASS_STORAGE && dev->subclass == PCI_SUBCLASS_AHCI) {
            ahci_dev = dev;
            break;
        }
    }

    if (!ahci_dev) {
        puts("AHCI: No controller found\r\n");
        return;
    }

    printf("AHCI: Found controller at %u:%u\r\n", ahci_dev->bus, ahci_dev->slot);

    // Read BAR5 (ABAR)
    u64 abar_phys = pci_read_bar(ahci_dev->bus, ahci_dev->slot, ahci_dev->func, 5);
    printf("AHCI: ABAR = 0x%x\r\n", abar_phys);

    // Map MMIO region (need enough for HBA + 32 ports)
    map_mmio(abar_phys, PAGE_SIZE * 4);
    hba = (struct hba_mem *)PHYS_TO_VIRT(abar_phys);

    // Enable AHCI mode
    hba->ghc |= HBA_GHC_AE;

    printf("AHCI: Version %x.%x, %u ports implemented\r\n",
           (hba->vs >> 16) & 0xFFFF, hba->vs & 0xFFFF,
           HBA_CAP_NP(hba->cap) + 1);

    // Initialize implemented ports
    u32 pi = hba->pi;
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1 << i)))
            continue;

        struct hba_port *port = &hba->ports[i];
        int type = ahci_port_type(port);

        if (type == AHCI_DEV_NULL)
            continue;

        const char *type_str = "unknown";
        if (type == AHCI_DEV_SATA) type_str = "SATA";
        else if (type == AHCI_DEV_SATAPI) type_str = "SATAPI";

        printf("AHCI: Port %d: %s\r\n", i, type_str);

        if (type == AHCI_DEV_SATA) {
            if (ahci_port_init(port) < 0) {
                printf("AHCI: Failed to init port %d\r\n", i);
                continue;
            }

            // Identify the drive
            u8 *id = kalloc();
            if (id && ahci_identify(port, id) == 0) {
                // Model string at words 27-46 (bytes 54-93), byte-swapped
                char model[41];
                for (int j = 0; j < 40; j += 2) {
                    model[j] = id[54 + j + 1];
                    model[j + 1] = id[54 + j];
                }
                model[40] = 0;
                // Trim trailing spaces
                for (int j = 39; j >= 0 && model[j] == ' '; j--)
                    model[j] = 0;

                printf("AHCI:   Model: %s\r\n", model);

                // LBA48 sector count at words 100-103 (bytes 200-207)
                u64 sectors = *(u64 *)&id[200];
                printf("AHCI:   Size: %u MB\r\n", (u32)(sectors / 2048));
            }
            if (id) kfree(id);
        }
    }

    puts("[ OK ] AHCI initialized\r\n");
}
